/*
 * E1-PB-LECA-AMB01 - nodo de gas y CO2 del LabECA (ESP32-C3 SuperMini), hoja de la mesh de AURA.
 * ESP-IDF 6.1 sobre la biblioteca nodo_mesh (Mesh_Red_Completa/componentes/nodo_mesh), contrato v4.4 (propuesta).
 *
 * Lo propio del dispositivo es esto: qué mide, qué parámetros tiene y qué comandos acepta. La
 * conexión, el aula, la cola, la confirmación de AURA y la hora las resuelve nodo_mesh.
 *
 * Sensores y salidas:
 *   MQ2 (Keyes) AO -> R1 10k -> GPIO3 -> R2 15k -> GND   (divisor 5 V -> 3 V)
 *   SCD40 (I2C)    SDA GPIO4, SCL GPIO5, 3V3
 *   LED RGB        módulo con resistencias: R GPIO0, G GPIO1, B GPIO6 (cátodo común)
 *   Buzzer         GPIO10 -> R3 1k -> base NPN (buzzer a 5 V)
 *
 * Lo que se publica en "values" (solo mediciones válidas, contrato 3.1):
 *   co2_ppm   ppm            (SCD40)
 *   temp_c    grados C       (SCD40)
 *   hum_pct   % HR           (SCD40)
 *   gas_mv    mV en el AO del MQ2, ya compensado por el divisor. Es una tensión, NO ppm:
 *             pasar a ppm requiere calibrar Ro con aire limpio.
 * Una sonda que falla no aparece en values: se avisa con nodo_mesh_sonda().
 *
 * LED de estado (led_patron.h): verde = conectado a la mesh, amarillo parpadeando = intentando
 * conectar, rojo = sin conexión (más de LED_REINTENTO_MS). Con LED_ESTADOS_EXTRA: azul que respira
 * mientras calienta (solo si ya está conectado: sin conexión se ve el intento), magenta rápido con
 * la alarma y cian (dos destellos) si falla un sensor.
 *
 * Calentamiento: los dos sensores arrancan juntos y comparten un solo reloj de CALENTAMIENTO_S
 * (2 min). Mientras dura no se publica nada, no se informa el estado de las sondas y la alarma y
 * el buzzer están inhibidos. Tras cualquier reinicio (por ejemplo un corte de luz) vuelve a
 * empezar completo.
 *
 * Dos ritmos distintos, una vez terminado el calentamiento:
 *   - cada SENSOR_PERIODO_MS (5 s) se mide, se evalúa la alarma local y se lleva la cuenta de las
 *     sondas (sonda.h).
 *   - cada intervalo_s (set_config, 5..86400 s) se publica la última medición válida y vigente de
 *     cada sonda. Si cambia una alarma se publica enseguida, sin esperar el intervalo.
 *
 * Alarma (alarma.h): gas y CO2 son causas separadas, cada una con su silencio. Cuando una causa se
 * activa o se normaliza se avisa a AURA con una alerta (dioxido_carbono_alto / gas_alto), así AURA
 * se entera aunque el intervalo sea largo y los umbrales viven en un solo lugar (alarma.h).
 *
 * Sondas (sonda.h): una falla se declara tras 3 lecturas malas seguidas y se recupera tras 2
 * buenas, y recién ahí se avisa a AURA (una alerta por cambio real). Si el SCD40 lleva 3 ciclos sin
 * dar lectura, se lo reinicia, y se reintenta cada 6 ciclos hasta que responda.
 *
 * Parámetro: intervalo_s. Comando propio: {"command":"silence"} calla el buzzer por las causas
 * activas hasta que se normalicen; sin alarma activa se rechaza ("sin_alarma"). El aula la asigna
 * AURA con set_config {"aula": ...}: no está acá.
 *
 * Todas las placas de este dispositivo llevan el MISMO binario: no hay MAC, id ni aula en el
 * código. MESH_ID y MESH_AP_PASS van en config_local.h (igual que en el gateway y el concentrador).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "cJSON.h"
#include "nodo_mesh.h"
#include "alarma.h"
#include "calentamiento.h"
#include "led.h"
#include "mq2.h"
#include "scd4x.h"
#include "sonda.h"

#if __has_include("config_local.h")
#include "config_local.h"
#else
#error "Falta main/config_local.h: copiá config_local.ejemplo.h como config_local.h y completá el ID y la clave de la mesh"
#endif

/* ---------------- Pines (ESP32-C3 SuperMini) ---------------- */
#define PIN_I2C_SDA          GPIO_NUM_4
#define PIN_I2C_SCL          GPIO_NUM_5
#define PIN_BUZZER           GPIO_NUM_10

/* Potencia en pasos de 0,25 dBm. La C3 SuperMini tiene la antena mal adaptada: 60 = 15 dBm,
   pensado para la antena de alambre de 31 mm (GUIA_NODOS_HOJA.md, sección 6). */
#define POTENCIA_TX          60

/* ---------------- Parámetros ---------------- */
#define INTERVALO_DEFECTO_S  60
#define INTERVALO_MIN_S      5
#define INTERVALO_MAX_S      86400
#define SENSOR_PERIODO_MS    5000      /* el SCD40 mide cada 5 s en modo periódico */
/* Calentamiento: CALENTAMIENTO_S (calentamiento.h, 2 min) desde que arrancan los dos sensores. */

/* 1 = el LED también muestra el calentamiento (azul), la alarma (magenta) y la falla de un sensor
   (cian). 0 = solo el estado de la conexión (verde / amarillo parpadeando / rojo). */
#define LED_ESTADOS_EXTRA    1
#define LED_PERIODO_MS       50

static const char *TAG = "AMB01";

static volatile uint32_t intervalo_s = INTERVALO_DEFECTO_S;

static SemaphoreHandle_t mtx_alarma;    /* protege alarma y el buzzer: silence llega de otra tarea */
static alarma_t alarma;

static i2c_master_bus_handle_t bus_i2c;                 /* para poder reiniciar el SCD40 */
static bool scd4x_ok = false;
static uint32_t scd4x_ciclos_sin_lectura = 0;           /* ciclos seguidos sin una lectura válida de CO2 */
static volatile bool sensor_con_falla = false;          /* alguna sonda declarada en falla (con histéresis) */
static volatile bool calentamiento_en_marcha = false;   /* true una vez que arrancaron los dos sensores */
static int64_t t_calentamiento_ms;        /* instante (desde el arranque del chip) en que arrancaron los sensores */

/* Estado de cada sonda: falla declarada y última medición válida (solo la usa la tarea de sensores) */
static sonda_t s_co2, s_temp, s_hum, s_gas;
static bool gas_saturado = false;         /* el ADC llegó a su techo en el último ciclo */
static bool publicar_ya = false;          /* cambió una alarma: publicar sin esperar el intervalo */

static uint32_t lecturas_ok = 0, lecturas_fallidas = 0;

/* ---------- Configuración remota (set_config) ---------- */
static bool aplicar_config(const cJSON *params, char *motivo, size_t tam)
{
    /* Primero se valida TODO; solo después se aplica (contrato §3.3) */
    if (!cJSON_IsObject(params) || cJSON_GetArraySize(params) == 0) {
        snprintf(motivo, tam, "sin_parametros");
        return false;
    }
    const cJSON *iv = NULL, *p;
    cJSON_ArrayForEach(p, params)
    {
        if (strcmp(p->string, "intervalo_s") == 0) {
            iv = p;
        } else if (strcmp(p->string, "aula") != 0) {      /* "aula" la maneja la biblioteca */
            snprintf(motivo, tam, "parametro_desconocido");
            return false;
        }
    }
    if (iv == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(iv) || iv->valuedouble != floor(iv->valuedouble)) {
        snprintf(motivo, tam, "intervalo_s_no_es_entero");
        return false;
    }
    if (iv->valuedouble < INTERVALO_MIN_S || iv->valuedouble > INTERVALO_MAX_S) {
        snprintf(motivo, tam, "intervalo_s_fuera_de_rango");
        return false;
    }
    intervalo_s = (uint32_t)iv->valuedouble;
    nvs_handle_t h;
    if (nvs_open("gasco2", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "intervalo_s", intervalo_s);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "config aplicada: intervalo_s=%lu", (unsigned long)intervalo_s);
    return true;
}

static void describir_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "intervalo_s", intervalo_s);
}

/* ---------- Comandos propios ---------- */
static void buzzer(bool suena)
{
    gpio_set_level(PIN_BUZZER, suena ? 1 : 0);
}

static bool ejecutar_comando(const char *comando, const cJSON *params, bool local, char *motivo, size_t tam)
{
    (void)params;
    if (strcmp(comando, "silence") == 0) {
        xSemaphoreTake(mtx_alarma, portMAX_DELAY);
        bool habia_alarma = alarma_silenciar(&alarma);
        if (habia_alarma) {
            buzzer(false);
        }
        xSemaphoreGive(mtx_alarma);
        if (!habia_alarma) {
            /* Sin alarma activa no hay nada que callar. Aceptarlo "para después" dejaría mudo el
               buzzer en la próxima alarma (p. ej. un silence durante el calentamiento). */
            snprintf(motivo, tam, "sin_alarma");
            ESP_LOGW(TAG, "silence rechazado (orden %s): no hay alarma activa", local ? "local" : "de AURA");
            return false;
        }
        ESP_LOGI(TAG, "buzzer silenciado (orden %s)", local ? "local" : "de AURA");
        return true;
    }
    snprintf(motivo, tam, "comando_desconocido");
    return false;
}

/* ---------- Diagnóstico ---------- */
static void escanear_i2c(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "escaneando el bus I2C...");
    int hallados = 0;
    for (uint8_t dir = 1; dir < 127; dir++) {
        if (i2c_master_probe(bus, dir, 20) == ESP_OK) {
            ESP_LOGI(TAG, "  dispositivo I2C en 0x%02X", dir);
            hallados++;
        }
    }
    if (hallados == 0) {
        ESP_LOGW(TAG, "  no se encontró ningún dispositivo I2C (revisar SDA/SCL/alimentación)");
    }
}

static int64_t ahora_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* Durante el calentamiento no se lee, no se publica y la alarma está inhibida. Devuelve true si el
   calentamiento sigue. */
static bool ciclo_calentamiento(void)
{
    uint64_t desde = (uint64_t)t_calentamiento_ms, ahora = (uint64_t)ahora_ms();
    if (!calentamiento_activo(desde, ahora, CALENTAMIENTO_S)) {
        return false;
    }
    uint32_t restante = calentamiento_restante_s(desde, ahora, CALENTAMIENTO_S);
    xSemaphoreTake(mtx_alarma, portMAX_DELAY);
    buzzer(false);                                  /* el buzzer queda callado y la alarma sin evaluar */
    xSemaphoreGive(mtx_alarma);
    ESP_LOGI(TAG, "calentando sensores: faltan %lu s (sin publicar, alarma inhibida)", (unsigned long)restante);
    return true;
}

/* Cuenta una lectura de una sonda. El aviso a AURA sale solo cuando cambia el estado declarado
   (histéresis en sonda.h); nodo_mesh_sonda además solo manda la alerta cuando hay cambio. */
static void registrar_sonda(sonda_t *s, const char *campo, bool ok, double valor, uint64_t t, const char *motivo)
{
    if (sonda_registrar(s, ok, valor, t)) {
        ESP_LOGW(TAG, "sonda %s: %s", campo, s->en_falla ? "FALLA declarada" : "recuperada");
    }
    nodo_mesh_sonda(campo, !s->en_falla, motivo);
}

/* Alerta a AURA cuando una causa de la alarma se activa o se normaliza (contrato 3.5). */
static void alertar_causa(const char *tipo, const char *severidad, bool activa, bool con_dato, const char *nombre,
                          const char *unidad, uint32_t valor, uint32_t umbral)
{
    char msg[80];
    cJSON *d = cJSON_CreateObject();
    if (d != NULL) {
        cJSON_AddNumberToObject(d, "valor", valor);
        cJSON_AddNumberToObject(d, "umbral", umbral);
    }
    if (activa) {
        snprintf(msg, sizeof(msg), "%s: %lu %s (umbral %lu %s)", nombre, (unsigned long)valor, unidad,
                 (unsigned long)umbral, unidad);
    } else if (con_dato) {
        snprintf(msg, sizeof(msg), "%s volvió a la normalidad (%lu %s)", nombre, (unsigned long)valor, unidad);
    } else {
        /* La causa se apagó porque la sonda dejó de dar dato, no porque se normalizó: no se afirma lo segundo */
        snprintf(msg, sizeof(msg), "%s: alarma terminada, la sonda no da dato", nombre);
    }
    nodo_mesh_alerta(tipo, activa ? severidad : "info", msg, d);
    cJSON_Delete(d);
}

/* ---------- Un ciclo de medición local (cada SENSOR_PERIODO_MS) ---------- */
static void ciclo_medicion(void)
{
    uint16_t co2 = 0;
    double temp = 0, hum = 0;
    bool co2_ok = false;
    uint64_t t = (uint64_t)ahora_ms();

    if (scd4x_ok) {
        esp_err_t e = scd4x_leer(&co2, &temp, &hum, 1500);
        if (e != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            e = scd4x_leer(&co2, &temp, &hum, 1500);      /* un reintento */
        }
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "error leyendo el SCD40: %s", esp_err_to_name(e));
        } else if (co2 == 0) {
            /* El SCD40 no mide 0 ppm: es una lectura sin sentido (típico del primer dato). */
            ESP_LOGW(TAG, "el SCD40 devolvió 0 ppm: lectura descartada");
        } else {
            co2_ok = true;
        }
    }
    if (co2_ok) {
        lecturas_ok++;
        scd4x_ciclos_sin_lectura = 0;
    } else {
        lecturas_fallidas++;
        scd4x_ciclos_sin_lectura++;
    }

    uint32_t gas_mv = 0;
    mq2_estado_t gas_est = mq2_leer(&gas_mv);
    bool gas_ok = (gas_est == MQ2_LECTURA_OK);
    gas_saturado = (gas_est == MQ2_SATURADA);

    /* Estado de las sondas hacia AURA, con histéresis. Solo se llega acá con el calentamiento
       terminado, así que una falla informada es una falla real. */
    registrar_sonda(&s_co2, "co2_ppm", co2_ok, co2, t, "sin_respuesta");
    registrar_sonda(&s_temp, "temp_c", co2_ok, temp, t, "sin_respuesta");
    registrar_sonda(&s_hum, "hum_pct", co2_ok, hum, t, "sin_respuesta");
    registrar_sonda(&s_gas, "gas_mv", gas_ok, gas_mv, t, gas_est == MQ2_SIN_RESPUESTA ? "sin_respuesta" : "fuera_de_rango");
    sensor_con_falla = s_co2.en_falla || s_gas.en_falla;

    /* SCD40 mudo varios ciclos seguidos (cable flojo, brownout, quedó trabado): se lo reinicia.
       Vale también si no respondió en el arranque: se reintenta cada tanto hasta que conteste. */
    if (bus_i2c != NULL && sonda_toca_reiniciar(scd4x_ciclos_sin_lectura)) {
        ESP_LOGW(TAG, "SCD40 sin lectura hace %lu ciclos: reiniciando el sensor", (unsigned long)scd4x_ciclos_sin_lectura);
        scd4x_ok = (scd4x_iniciar(bus_i2c) == ESP_OK);
    }

    /* Alarma local (no depende de la mesh). Usa la última medición VÁLIDA y vigente de cada sonda:
       una lectura mala suelta no apaga la alarma ni la rearma. Un MQ2 saturado cuenta como gas
       alto: el ADC llegó a su techo y el gas es al menos MQ2_AO_SATURADO_MV. */
    double v;
    bool co2_v = sonda_vigente(&s_co2, t, &v);
    uint16_t co2_a = co2_v ? (uint16_t)v : 0;
    bool gas_v = sonda_vigente(&s_gas, t, &v);
    uint32_t gas_a = gas_v ? (uint32_t)v : 0;
    if (gas_saturado) {
        gas_v = true;
        gas_a = MQ2_AO_SATURADO_MV;
    }

    xSemaphoreTake(mtx_alarma, portMAX_DELAY);
    bool gas_antes = alarma.gas_activa, co2_antes = alarma.co2_activa;
    bool suena = alarma_actualizar(&alarma, gas_v, gas_a, co2_v, co2_a);
    buzzer(suena);
    bool en_alarma = alarma.activa;
    bool gas_ahora = alarma.gas_activa, co2_ahora = alarma.co2_activa;
    xSemaphoreGive(mtx_alarma);

    /* Cada vez que una causa se activa o se normaliza se avisa a AURA y se publica ya: el aviso no
       depende del intervalo_s (que puede ser de horas) ni de que la mesh esté arriba en ese instante
       (la cola de nodo_mesh lo guarda). */
    if (co2_ahora != co2_antes) {
        alertar_causa("dioxido_carbono_alto", "warning", co2_ahora, co2_v, "CO2", "ppm", co2_a, co2_ahora ? CO2_ALARMA_PPM : CO2_NORMAL_PPM);
        publicar_ya = true;
    }
    if (gas_ahora != gas_antes) {
        alertar_causa("gas_alto", "high", gas_ahora, gas_v, gas_saturado ? "Gas (MQ2, saturado)" : "Gas (MQ2)", "mV", gas_a,
                      gas_ahora ? GAS_ALARMA_MV : GAS_NORMAL_MV);
        publicar_ya = true;
    }

    ESP_LOGI(TAG, "CO2 %s%u ppm | T %.1f C | HR %.1f %% | gas %s%lu mV | alarma %s%s", co2_ok ? "" : "(sin dato) ", co2,
             temp, hum, gas_ok ? "" : (gas_saturado ? "(saturado) " : "(inválido) "), (unsigned long)gas_mv,
             en_alarma ? "ACTIVA" : "ok", (en_alarma && !suena) ? " (buzzer silenciado)" : "");
    ESP_LOGI(TAG, "SCD40: %lu lecturas ok, %lu fallidas | mesh: %lu pendientes de confirmar, aula '%s'",
             (unsigned long)lecturas_ok, (unsigned long)lecturas_fallidas, (unsigned long)nodo_mesh_pendientes(),
             nodo_mesh_aula());
}

/* ---------- Publicación a la mesh (cada intervalo_s) ---------- */
/* Publica la última medición válida y vigente de cada sonda (no la última lectura, que puede ser
   una mala suelta). Devuelve true solo si se guardó algo para enviar. */
static bool publicar(uint64_t t)
{
    cJSON *v = cJSON_CreateObject();
    if (v == NULL) {
        return false;
    }
    double x;
    if (sonda_vigente(&s_co2, t, &x)) {
        cJSON_AddNumberToObject(v, "co2_ppm", x);
    }
    if (sonda_vigente(&s_temp, t, &x)) {
        /* double con un decimal: cJSON escribe 24.1 y no 24.100000381 */
        cJSON_AddNumberToObject(v, "temp_c", round(x * 10.0) / 10.0);
    }
    if (sonda_vigente(&s_hum, t, &x)) {
        cJSON_AddNumberToObject(v, "hum_pct", round(x * 10.0) / 10.0);
    }
    if (sonda_vigente(&s_gas, t, &x)) {
        cJSON_AddNumberToObject(v, "gas_mv", x);
    }
    /* Si ninguna sonda tiene un valor vigente no se manda nada (contrato 3.1) */
    bool enviada = cJSON_GetArraySize(v) > 0 && nodo_mesh_medicion(v);
    cJSON_Delete(v);
    return enviada;
}

/* LED de estado: se arranca antes que la mesh, así que mientras la mesh arranca muestra "intentando". */
static void tarea_led(void *arg)
{
    int64_t t_ultima_conexion_ms = 0;          /* 0 = desde el arranque, si nunca se conectó */
    while (true) {
        int64_t ahora = ahora_ms();
        bool conectada = nodo_mesh_conectada();
        if (conectada) {
            t_ultima_conexion_ms = ahora;
        }
        led_entrada_t e = {
            .conectada = conectada,
            .sin_conexion_ms = (uint64_t)(ahora - t_ultima_conexion_ms),
#if LED_ESTADOS_EXTRA
            .calentando = !calentamiento_en_marcha ||
                          calentamiento_activo((uint64_t)t_calentamiento_ms, (uint64_t)ahora, CALENTAMIENTO_S),
            .alarma = alarma.activa,
            .sensor_falla = sensor_con_falla,
#endif
        };
        led_poner(led_calcular(&e, (uint64_t)ahora));
        vTaskDelay(pdMS_TO_TICKS(LED_PERIODO_MS));
    }
}

static void tarea_sensores(void *arg)
{
    TickType_t base = xTaskGetTickCount();
    int64_t t_publicacion_ms = 0;
    bool ya_publico = false;
    while (true) {
        if (!ciclo_calentamiento()) {
            ciclo_medicion();

            /* Terminado el calentamiento se publica enseguida y después cada intervalo_s, siempre
               que haya alguna medición válida (contrato 3.1). Una sola publicación lleva los
               valores de los dos sensores juntos. */
            int64_t ahora = ahora_ms();
            if (publicar_ya || !ya_publico || ahora - t_publicacion_ms >= (int64_t)intervalo_s * 1000) {
                if (publicar((uint64_t)ahora)) {      /* si no había nada que enviar, se reintenta en el ciclo siguiente */
                    ya_publico = true;
                    publicar_ya = false;
                    t_publicacion_ms = ahora;
                }
            }
        }
        vTaskDelayUntil(&base, pdMS_TO_TICKS(SENSOR_PERIODO_MS));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    mtx_alarma = xSemaphoreCreateMutex();
    gpio_reset_pin(PIN_BUZZER);
    gpio_set_direction(PIN_BUZZER, GPIO_MODE_OUTPUT);
    buzzer(false);

    nvs_handle_t h;
    if (nvs_open("gasco2", NVS_READONLY, &h) == ESP_OK) {
        uint32_t v;
        if (nvs_get_u32(h, "intervalo_s", &v) == ESP_OK && v >= INTERVALO_MIN_S && v <= INTERVALO_MAX_S) {
            intervalo_s = v;
        }
        nvs_close(h);
    }

    /* LED de estado antes que nada: así se ve "intentando" desde el arranque */
    if (led_iniciar() == ESP_OK) {
        xTaskCreate(tarea_led, "led", 3072, NULL, 3, NULL);
    } else {
        ESP_LOGE(TAG, "no pude iniciar el LED RGB");
    }

    if (mq2_iniciar() != ESP_OK) {
        ESP_LOGE(TAG, "el MQ2 no se pudo iniciar: gas_mv saldrá como sonda con falla (sin_respuesta)");
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &bus_i2c) == ESP_OK) {
        escanear_i2c(bus_i2c);
        scd4x_ok = (scd4x_iniciar(bus_i2c) == ESP_OK);
    } else {
        ESP_LOGE(TAG, "no pude crear el bus I2C: sin SCD40");
    }

    /* Un solo reloj para los dos sensores: arranca ahora que el MQ2 y el SCD40 ya están iniciados */
    t_calentamiento_ms = ahora_ms();
    calentamiento_en_marcha = true;
    ESP_LOGI(TAG, "calentando los sensores %u s: no se publica ni suena la alarma hasta terminar", CALENTAMIENTO_S);

    nodo_mesh_config_t cfg = {
        .mesh_id = {MESH_ID_BYTES},
        .mesh_clave = MESH_AP_PASS,
        .potencia_tx = POTENCIA_TX,
        .alimentacion = "red",
        .aplicar_config = aplicar_config,
        .describir_config = describir_config,
        .ejecutar_comando = ejecutar_comando,
    };
    ESP_ERROR_CHECK(nodo_mesh_iniciar(&cfg));
    ESP_LOGI(TAG, "nodo Gas/CO2 listo, publica cada %lu s", (unsigned long)intervalo_s);
    xTaskCreate(tarea_sensores, "sensores", 6144, NULL, 4, NULL);
}
