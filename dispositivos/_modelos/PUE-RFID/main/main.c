/*
 * Puerta con llavero de proximidad (modelo PUE-RFID): hoja de la mesh de AURA, ESP-IDF.
 * ESP32-C3 SuperMini + lector RC522 + modulo de rele de la cerradura + sensor magnetico +
 * buzzer. Reglas en docs/adr/ADR-009 y en la ficha de este modelo (README.md).
 *
 * Como esta partido:
 *   puerta_logica.h  decide (maquina de estados pura, probada en la compu)
 *   este archivo     cablea: lee el sensor y el lector, ejecuta lo que la logica decide
 *   rele.c           el unico que toca la cerradura; su temporizador la traba aunque esto se cuelgue
 *   sonido.c         tarea propia (nada de delay() en la logica)
 *   lector.c         RC522
 *   almacen.c        NVS, escribiendo solo cuando algo cambia
 *
 * Todas las puertas llevan este MISMO binario: no hay MAC, id, aula ni nada de una puerta en
 * el codigo. Lo que cambia de una a otra (tiempo del pulso, aviso, tipo de sensor) va por
 * set_config; la lista de llaveros, por tarjeta_agregar / tarjeta_quitar.
 *
 * MESH_ID y MESH_AP_PASS: en config_local.h (igual que en el gateway y el concentrador).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "nodo_mesh.h"

#include "almacen.h"
#include "config_validacion.h"
#include "lector.h"
#include "latido_logica.h"
#include "lista_tarjetas.h"
#include "pines.h"
#include "puerta_logica.h"
#include "rele.h"
#include "sensor_logica.h"
#include "sonido.h"

#if __has_include("config_local.h")
#include "config_local.h"
#else
#error "Falta main/config_local.h: copiá config_local.ejemplo.h como config_local.h y completá el ID y la clave de la mesh"
#endif

#if CONFIG_PUERTA_CARGA_POR_CONSOLA
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#endif

static const char *TAG = "PUERTA";

/* Potencia en pasos de 0,25 dBm (60 = 15 dBm). Igual que la hoja de ejemplo de la catedra. */
#define POTENCIA_TX 60
#define PERIODO_LOGICA_MS 10
#define PERIODO_LECTOR_MS 5000
#define LECTOR_CAIDO_REINICIO_MS (5 * 60 * 1000)   /* un lector que no vuelve en 5 min: se reinicia la placa */

/* ---------- Estado compartido con la mesh ---------- */
static SemaphoreHandle_t lock_lista;       /* protege `lista` (lecturas cortas) */
static SemaphoreHandle_t lock_cambio_lista;   /* serializa un cambio entero: copiar, modificar, guardar, asignar */
static lista_t lista;                      /* lo que decide quien entra; lo cambia AURA */
static SemaphoreHandle_t lock_cfg;
static config_puerta_t cfg;                /* set_config */
static latido_t latido;                    /* intervalo informado y cuando toca el proximo latido */
static QueueHandle_t cola_lecturas;
static QueueHandle_t cola_abrir;           /* ordenes de abrir de AURA, hacia la tarea de la logica */

/* Solo la tarea de la logica toca esto */
static puerta_t puerta;

static inline int64_t ahora_ms(void) { return esp_timer_get_time() / 1000; }

/* ================================================================== */
/*  Lista de llaveros                                                  */
/* ================================================================== */
/* Persiste primero y recien despues cambia la lista viva: si la flash falla, no queda una lista
   en RAM distinta de la guardada. Todo el cambio va bajo lock_cambio_lista: dos cambios a la vez
   (AURA y la consola de banco) no se pisan. lock_lista se toma solo para copiar y para asignar,
   asi la tarea de la logica no espera la escritura en flash al leer un llavero. */
static lista_resultado_t lista_cambiar(bool agregar, const uint8_t *uid, size_t largo, bool *guardado)
{
    xSemaphoreTake(lock_cambio_lista, portMAX_DELAY);
    lista_t copia;
    xSemaphoreTake(lock_lista, portMAX_DELAY);
    copia = lista;
    xSemaphoreGive(lock_lista);

    lista_resultado_t r = agregar ? lista_agregar(&copia, uid, largo) : lista_quitar(&copia, uid, largo);
    *guardado = true;
    if (r == LISTA_OK) {                   /* ya estaba / no estaba / llena / invalido: no cambia nada */
        if (almacen_guardar_lista(&copia)) {
            xSemaphoreTake(lock_lista, portMAX_DELAY);
            lista = copia;
            xSemaphoreGive(lock_lista);
        } else {
            *guardado = false;
        }
    }
    xSemaphoreGive(lock_cambio_lista);
    return r;
}

/* ================================================================== */
/*  Lo que aporta el dispositivo a nodo_mesh                           */
/* ================================================================== */
static const char *sensor_nombre(sensor_tipo_t s) { return s == SENSOR_BOTON ? "boton" : "reed"; }

static bool aplicar_config(const cJSON *params, char *motivo, size_t tam)
{
    /* params tiene que ser un objeto: un arreglo ("params":[5000]) no trae nombres de campo */
    if (!cJSON_IsObject(params)) {
        snprintf(motivo, tam, "params_invalido");
        return false;
    }
    /* Primero se valida TODO; solo despues se aplica (contrato 3.3) */
    config_puerta_t nueva;
    xSemaphoreTake(lock_cfg, portMAX_DELAY);
    nueva = cfg;
    xSemaphoreGive(lock_cfg);

    const cJSON *p;
    cJSON_ArrayForEach(p, params) {
        if (p->string == NULL) {
            snprintf(motivo, tam, "params_invalido");
            return false;
        }
        if (strcmp(p->string, "aula") == 0) continue;           /* la maneja la biblioteca */
        if (strcmp(p->string, "pulso_ms") == 0) {
            int32_t valor;
            if (!cJSON_IsNumber(p) || !entero_en_rango(p->valuedouble, PULSO_MINIMO_MS, PULSO_TOPE_MS, &valor)) {
                snprintf(motivo, tam, "pulso_ms_fuera_de_rango");
                return false;
            }
            nueva.pulso_ms = valor;
        } else if (strcmp(p->string, "aviso_s") == 0) {
            int32_t valor;
            if (!cJSON_IsNumber(p) || !entero_en_rango(p->valuedouble, AVISO_MIN_S, AVISO_MAX_S, &valor)) {
                snprintf(motivo, tam, "aviso_s_fuera_de_rango");
                return false;
            }
            nueva.aviso_s = valor;
        } else if (strcmp(p->string, "sensor") == 0) {
            if (cJSON_IsString(p) && strcmp(p->valuestring, "reed") == 0) {
                nueva.sensor = SENSOR_REED;
            } else if (cJSON_IsString(p) && strcmp(p->valuestring, "boton") == 0) {
                nueva.sensor = SENSOR_BOTON;
            } else {
                snprintf(motivo, tam, "sensor_invalido");
                return false;
            }
        } else {
            snprintf(motivo, tam, "parametro_desconocido");
            return false;
        }
    }
    if (!almacen_guardar_config(&nueva)) {
        snprintf(motivo, tam, "error_de_nvs");
        return false;
    }
    xSemaphoreTake(lock_cfg, portMAX_DELAY);
    cfg = nueva;
    xSemaphoreGive(lock_cfg);
    ESP_LOGI(TAG, "Config: pulso %d ms, aviso %d s, sensor %s", (int)nueva.pulso_ms, (int)nueva.aviso_s,
             sensor_nombre(nueva.sensor));
    return true;
}

static void describir_config(cJSON *config)
{
    config_puerta_t k;
    lista_t l;
    xSemaphoreTake(lock_cfg, portMAX_DELAY);
    k = cfg;
    xSemaphoreGive(lock_cfg);
    xSemaphoreTake(lock_lista, portMAX_DELAY);
    l = lista;
    xSemaphoreGive(lock_lista);

    char hash[HASH_HEX_BYTES];
    lista_hash(&l, hash);
    /* intervalo_s: la raiz infiere que la placa se cayo con 3 intervalos sin datos. Armada, el
       latido es mas seguido y por eso este numero cambia (ADR-009, decision 7). */
    cJSON_AddNumberToObject(config, "intervalo_s", latido.intervalo_s);
    cJSON_AddNumberToObject(config, "pulso_ms", k.pulso_ms);
    cJSON_AddNumberToObject(config, "aviso_s", k.aviso_s);
    cJSON_AddStringToObject(config, "sensor", sensor_nombre(k.sensor));
    cJSON_AddNumberToObject(config, "tarjetas", l.n);       /* cuantos llaveros y su hash: AURA los compara con los suyos */
    cJSON_AddStringToObject(config, "hash", hash);
}

static bool comando_tarjeta(bool agregar, const cJSON *params, char *motivo, size_t tam)
{
    const cJSON *u = cJSON_GetObjectItemCaseSensitive(params, "uid");
    uint8_t uid[UID_MAX_BYTES];
    uint8_t largo;
    if (!cJSON_IsString(u) || !hex_a_uid(u->valuestring, uid, &largo)) {
        snprintf(motivo, tam, "uid_invalido");
        return false;
    }
    bool guardado;
    lista_resultado_t r = lista_cambiar(agregar, uid, largo, &guardado);
    if (!guardado) {
        snprintf(motivo, tam, "error_de_nvs");
        return false;
    }
    if (r == LISTA_LLENA) {
        snprintf(motivo, tam, "lista_llena");
        return false;
    }
    if (r == LISTA_UID_INVALIDO) {
        snprintf(motivo, tam, "uid_invalido");
        return false;
    }
    /* "ya estaba" y "no estaba" son exitos: AURA reintenta hasta que la puerta confirma, y un
       reintento no puede fallar porque el primero ya se aplico */
    ESP_LOGI(TAG, "Llavero %s %s%s", u->valuestring, agregar ? "agregado" : "quitado",
             r == LISTA_OK ? "" : " (ya estaba asi)");
    nodo_mesh_reportar();                  /* que AURA vea enseguida la cantidad y el hash nuevos */
    return true;
}

static bool ejecutar_comando(const char *comando, const cJSON *params, bool local, char *motivo, size_t tam)
{
    /* Las ordenes del boton del concentrador (local) no pueden tocar una puerta */
    if (local && strcmp(comando, "estado") != 0) {
        snprintf(motivo, tam, "orden_local_no_admitida");
        return false;
    }
    if (strcmp(comando, "tarjeta_agregar") == 0) return comando_tarjeta(true, params, motivo, tam);
    if (strcmp(comando, "tarjeta_quitar") == 0) return comando_tarjeta(false, params, motivo, tam);
    if (strcmp(comando, "estado") == 0) {
        /* Solo lectura: manda el reporte de la placa y un latido con la puerta y el armado */
        latido.latir_ya = true;
        nodo_mesh_reportar();
        return true;
    }
    if (strcmp(comando, "abrir") == 0) {
#if CONFIG_PUERTA_ABRIR_REMOTO
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(params, "vence");
        bool tiene = cJSON_IsNumber(v);
        orden_estado_t o = orden_vigente(tiene, tiene ? (int64_t)v->valuedouble : 0, (int64_t)time(NULL));
        if (o != ORDEN_OK) {
            static const char *const motivos[] = {"", "sin_vencimiento", "sin_hora_valida", "orden_vencida",
                                                  "vencimiento_muy_lejano"};
            snprintf(motivo, tam, "%s", motivos[o]);
            return false;
        }
        uint8_t pedido = 1;
        if (xQueueSend(cola_abrir, &pedido, 0) != pdTRUE) {
            snprintf(motivo, tam, "ocupada");
            return false;
        }
        return true;
#else
        snprintf(motivo, tam, "abrir_deshabilitado");      /* ADR-009, decision 8 */
        return false;
#endif
    }
    snprintf(motivo, tam, "comando_desconocido");
    return false;
}

/* ================================================================== */
/*  Registro y alertas                                                 */
/* ================================================================== */
static const char *severidad_texto(severidad_t s)
{
    switch (s) {
    case SEV_INFO: return "info";
    case SEV_WARNING: return "warning";
    case SEV_HIGH: return "high";
    case SEV_CRITICAL: return "critical";
    }
    return "info";
}

/* Cada evento va como data con su ingest_id (lo pone nodo_mesh): si no hay red queda en la cola
   de la placa y no se pierde. Por la mesh viaja el UID; la persona la asocia AURA. */
static void emitir_evento(const accion_t *a)
{
    cJSON *v = cJSON_CreateObject();
    if (v == NULL) return;
    cJSON_AddStringToObject(v, "evento", a->texto);
    if (a->uid_largo > 0) {
        char hex[UID_HEX_MAX];
        uid_a_hex(a->uid, a->uid_largo, hex);
        cJSON_AddStringToObject(v, "uid", hex);
    }
    if (a->de_la_app) cJSON_AddStringToObject(v, "origen", "app");
    cJSON_AddNumberToObject(v, "puerta", puerta.abierta ? 1 : 0);
    cJSON_AddNumberToObject(v, "armada", puerta.armada ? 1 : 0);
    nodo_mesh_medicion(v);
    cJSON_Delete(v);
}

static void emitir_latido(void)
{
    cJSON *v = cJSON_CreateObject();
    if (v == NULL) return;
    cJSON_AddNumberToObject(v, "puerta", puerta.abierta ? 1 : 0);
    cJSON_AddNumberToObject(v, "armada", puerta.armada ? 1 : 0);
    cJSON_AddNumberToObject(v, "cerradura", rele_energizado() ? 1 : 0);
    nodo_mesh_medicion(v);
    cJSON_Delete(v);
}

/* Ejecuta, en el orden en que la logica las produjo, lo que la logica decidio. La alerta de
   intrusion es la primera accion de su tanda, asi que es lo primero que sale. */
static void ejecutar_acciones(void)
{
    accion_t a;
    while (puerta_sacar(&puerta, &a)) {
        switch (a.tipo) {
        case ACC_PULSO:
            rele_energizar((uint32_t)a.valor);
            ESP_LOGI(TAG, "Cerradura destrabada %d ms", (int)a.valor);
            break;
        case ACC_CORTAR_PULSO:
            rele_soltar();
            ESP_LOGI(TAG, "Cerradura trabada");
            break;
        case ACC_SONIDO:
            sonido_puntual((sonido_t)a.valor);
            break;
        case ACC_AVISO:
            sonido_aviso_fijar((aviso_t)a.valor, a.desde_ms, a.hasta_ms);
            break;
        case ACC_EVENTO:
            ESP_LOGI(TAG, "Evento: %s", a.texto);
            emitir_evento(&a);
            break;
        case ACC_ALERTA:
            ESP_LOGW(TAG, "ALERTA %s (%s): %s", a.texto, severidad_texto(a.severidad), a.mensaje);
            nodo_mesh_alerta(a.texto, severidad_texto(a.severidad), a.mensaje, NULL);
            break;
        case ACC_GUARDAR_ARMADA:
            almacen_guardar_armada(a.valor != 0);
            break;
        case ACC_REPORTAR:
            /* el intervalo_s del reporte tiene que ser el de ahora (armada o no), no el anterior */
            latido_al_reportar(&latido, &puerta);
            nodo_mesh_reportar();
            break;
        }
    }
}

/* ================================================================== */
/*  Tarea de la logica                                                 */
/* ================================================================== */
static void tarea_puerta(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    config_puerta_t k;
    xSemaphoreTake(lock_cfg, portMAX_DELAY);
    k = cfg;
    xSemaphoreGive(lock_cfg);

    vTaskDelay(pdMS_TO_TICKS(100));        /* que la pull-up del sensor se estabilice */
    sensor_filtro_t sensor;
    sensor_filtro_iniciar(&sensor, k.sensor, gpio_get_level(PIN_SENSOR), ahora_ms());
    bool estable = sensor.estable;

    puerta_iniciar(&puerta, ahora_ms(), almacen_cargar_armada(), estable, k.pulso_ms, k.aviso_s);
    ESP_LOGI(TAG, "Arranca %s, puerta %s", puerta.armada ? "ARMADA" : "desarmada", estable ? "abierta" : "cerrada");

    int64_t proximo_lector = ahora_ms() + PERIODO_LECTOR_MS, lector_caido_desde = 0;
    bool sensor_sospechoso = false;

    while (true) {
        esp_task_wdt_reset();
        int64_t ahora = ahora_ms();

        /* 1. El sensor, antes que nada: una intrusion tiene que salir primero */
        if (xSemaphoreTake(lock_cfg, 0) == pdTRUE) {
            k = cfg;
            xSemaphoreGive(lock_cfg);
        }
        puerta.pulso_ms = k.pulso_ms;
        puerta.aviso_s = k.aviso_s;
        bool abierta;
        switch (sensor_filtro_paso(&sensor, ahora, k.sensor, gpio_get_level(PIN_SENSOR), &abierta)) {
        case SENSOR_CAMBIO:
            ESP_LOGI(TAG, "Puerta %s", abierta ? "ABIERTA" : "cerrada");
            puerta_sensor(&puerta, ahora, abierta);
            break;
        case SENSOR_RESINCRONIZADO:
            /* set_config cambio el tipo de sensor: la lectura nueva es el estado, no un movimiento */
            ESP_LOGI(TAG, "Sensor %s: la puerta se ve %s", sensor_nombre(k.sensor), abierta ? "abierta" : "cerrada");
            puerta_sensor_resincronizar(&puerta, ahora, abierta);
            break;
        case SENSOR_IGUAL:
            break;
        }
        ejecutar_acciones();

        /* 2. Orden de abrir de AURA (solo llega si se compilo habilitada) */
        uint8_t pedido;
        while (xQueueReceive(cola_abrir, &pedido, 0) == pdTRUE) {
            ESP_LOGI(TAG, "Orden de abrir (app)");
            puerta_abrir_remoto(&puerta, ahora);
            ejecutar_acciones();
        }

        /* 3. Llaveros */
        lectura_t l;
        while (xQueueReceive(cola_lecturas, &l, 0) == pdTRUE) {
            char hex[UID_HEX_MAX];
            uid_a_hex(l.uid, l.largo, hex);
            xSemaphoreTake(lock_lista, portMAX_DELAY);
            bool autorizada = lista_autorizada(&lista, l.uid, l.largo);
            xSemaphoreGive(lock_lista);
            ESP_LOGI(TAG, "Llavero %s -> %s", hex, autorizada ? "autorizado" : "RECHAZADO");
            puerta_tarjeta(&puerta, ahora, l.uid, l.largo, autorizada);
            ejecutar_acciones();
        }

        /* 4. El paso del tiempo: vence el pulso, los avisos, los topes */
        puerta_tick(&puerta, ahora);
        ejecutar_acciones();

        /* 5. Salud del lector y del sensor, cada tanto */
        if ((ahora - proximo_lector) >= 0) {
            proximo_lector = ahora + PERIODO_LECTOR_MS;
            lector_salud_t s = lector_revisar();
            if (s == LECTOR_CAIDO) {
                if (lector_caido_desde == 0) lector_caido_desde = ahora;
                nodo_mesh_sonda("lector", false, "sin_respuesta");
                if ((ahora - lector_caido_desde) >= LECTOR_CAIDO_REINICIO_MS) {
                    ESP_LOGE(TAG, "El lector no vuelve: reinicio la placa (queda trabada)");
                    esp_restart();
                }
            } else {
                lector_caido_desde = 0;
                nodo_mesh_sonda("lector", true, NULL);
            }
            bool sospechoso = puerta_sensor_sospechoso(&puerta, ahora);
            if (sospechoso != sensor_sospechoso) {
                sensor_sospechoso = sospechoso;
                nodo_mesh_sonda("puerta", !sospechoso, "abierta_demasiado_tiempo");
            }
        }

        /* 6. Latido: mas seguido con la puerta armada (se supervisa que no la hayan arrancado) */
        if (latido_toca(&latido, &puerta, ahora)) emitir_latido();

        vTaskDelay(pdMS_TO_TICKS(PERIODO_LOGICA_MS));
    }
}

/* ================================================================== */
/*  Carga de llaveros por consola (SOLO PARA BANCO)                    */
/* ================================================================== */
#if CONFIG_PUERTA_CARGA_POR_CONSOLA
static void tarea_consola(void *arg)
{
    (void)arg;
    usb_serial_jtag_driver_config_t uc = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&uc);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, NULL, _IONBF, 0);
    printf("\nCONSOLA DE BANCO: agregar <UID> | quitar <UID> | listar | config <clave> <valor>\n");
    char linea[64];
    while (fgets(linea, sizeof(linea), stdin) != NULL) {
        char orden[16] = "", uidtxt[32] = "";
        sscanf(linea, "%15s %31s", orden, uidtxt);
        if (strcmp(orden, "listar") == 0) {
            lista_t l;
            xSemaphoreTake(lock_lista, portMAX_DELAY);
            l = lista;
            xSemaphoreGive(lock_lista);
            char hash[HASH_HEX_BYTES];
            lista_hash(&l, hash);
            printf("%d llaveros, hash %s\n", l.n, hash);
            for (int i = 0; i < l.n; i++) {
                char hex[UID_HEX_MAX];
                uid_a_hex(l.t[i].uid, l.t[i].largo, hex);
                printf("  %s\n", hex);
            }
        } else if (strcmp(orden, "agregar") == 0 || strcmp(orden, "quitar") == 0) {
            uint8_t uid[UID_MAX_BYTES];
            uint8_t largo;
            bool agregar = orden[0] == 'a';
            bool guardado;
            if (!hex_a_uid(uidtxt, uid, &largo)) {
                printf("UID invalido (hexa de 4, 7 o 10 bytes)\n");
            } else {
                lista_resultado_t r = lista_cambiar(agregar, uid, largo, &guardado);
                printf("%s\n", !guardado ? "error de NVS" : r == LISTA_OK ? "hecho" : r == LISTA_LLENA ? "lista llena" : "ya estaba asi");
                if (guardado && r == LISTA_OK) nodo_mesh_reportar();
            }
        } else if (strcmp(orden, "config") == 0) {
            /* Lo mismo que un set_config de AURA (misma validacion), para probar sin gateway:
               config sensor boton | config pulso_ms 3000 | config aviso_s 10 */
            char clave[16] = "", valor[16] = "";
            sscanf(linea, "%*s %15s %15s", clave, valor);
            cJSON *params = cJSON_CreateObject();
            char* fin = NULL;
            double num = strtod(valor, &fin);
            if (valor[0] != '\0' && *fin == '\0') {
                cJSON_AddNumberToObject(params, clave, num);
            } else {
                cJSON_AddStringToObject(params, clave, valor);
            }
            char motivo[48] = "";
            bool ok = clave[0] != '\0' && aplicar_config(params, motivo, sizeof(motivo));
            printf("%s\n", ok ? "hecho" : motivo[0] ? motivo : "uso: config <clave> <valor>");
            cJSON_Delete(params);
        } else if (orden[0] != '\0') {
            printf("agregar <UID> | quitar <UID> | listar | config <clave> <valor>\n");
        }
    }
    vTaskDelete(NULL);
}
#endif

/* ================================================================== */
void app_main(void)
{
    /* LO PRIMERO: la cerradura en reposo (trabada), antes de NVS, del lector, de la mesh y de
       todo lo demas. Un reinicio nunca puede dejarla destrabada. */
    rele_reposo_inicial();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    rele_iniciar();

    gpio_config_t entrada = {
        .pin_bit_mask = 1ULL << PIN_SENSOR,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&entrada));

    lock_lista = xSemaphoreCreateMutex();
    lock_cambio_lista = xSemaphoreCreateMutex();
    latido_iniciar(&latido, LATIDO_DESARMADA_S);       /* antes de la mesh: describir_config lo lee */
    lock_cfg = xSemaphoreCreateMutex();
    cola_lecturas = xQueueCreate(4, sizeof(lectura_t));
    cola_abrir = xQueueCreate(2, sizeof(uint8_t));

    almacen_iniciar();
    almacen_cargar_lista(&lista);
    almacen_cargar_config(&cfg);
    ESP_LOGI(TAG, "Llaveros guardados: %d", lista.n);   /* una placa nueva arranca con 0: no abre con ninguno */

    sonido_iniciar();

    err = lector_iniciar(cola_lecturas);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "El lector RC522 no arranca (%s): sin lector no se abre con llavero", esp_err_to_name(err));
    }

    nodo_mesh_config_t mesh = {
        .mesh_id = {MESH_ID_BYTES},
        .mesh_clave = MESH_AP_PASS,
        .potencia_tx = POTENCIA_TX,
        .alimentacion = "red",
        .aplicar_config = aplicar_config,
        .describir_config = describir_config,
        .ejecutar_comando = ejecutar_comando,
    };
    ESP_ERROR_CHECK(nodo_mesh_iniciar(&mesh));
    if (err != ESP_OK) nodo_mesh_sonda("lector", false, "sin_respuesta");

    xTaskCreate(tarea_puerta, "puerta", 6144, NULL, 5, NULL);
#if CONFIG_PUERTA_CARGA_POR_CONSOLA
    xTaskCreate(tarea_consola, "consola", 4096, NULL, 2, NULL);
#endif
}
