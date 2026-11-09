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
 * (2 min). Mientras dura no se publica nada, no se informa el estado de las sondas y el aviso de
 * humo/vapores (MQ2) está inhibido. El aviso de CO2 NO espera: el SCD40 da lecturas a los pocos
 * segundos y el filtro de 1 minuto de alarma.h cubre las primeras. Tras cualquier reinicio (por
 * ejemplo un corte de luz) todo vuelve a empezar: las causas arrancan en normal y, si siguen
 * altas, el aviso se prende al minuto.
 *
 * Dos ritmos distintos, una vez terminado el calentamiento:
 *   - cada SENSOR_PERIODO_MS (5 s) se mide, se evalúa el aviso local y se lleva la cuenta de las
 *     sondas (sonda.h).
 *   - cada intervalo_s (set_config, 5..86400 s) se publica la última medición válida y vigente de
 *     cada sonda. Si cambia el estado de una causa se publica enseguida, sin esperar el intervalo.
 *
 * Aviso de ventilación (alarma.h): CO2 y humo/vapores son causas independientes con tres estados
 * (normal / activa / silenciada). Lo decide el nodo: suena igual sin mesh y sin AURA. El buzzer da
 * un pitido corto cada tanto (no sirena) y una tarea propia lo maneja. Cada cambio de estado de una
 * causa se avisa a AURA con una alerta (dioxido_carbono_alto / humo_vapores, details: estado, valor,
 * umbral, saturado); la cola de nodo_mesh la guarda si la mesh está caída.
 *
 * Sondas (sonda.h): una falla se declara tras 3 lecturas malas seguidas y se recupera tras 2
 * buenas, y recién ahí se avisa a AURA (una alerta por cambio real). Si el SCD40 lleva 3 ciclos sin
 * dar lectura, se lo reinicia, y se reintenta cada 6 ciclos hasta que responda. Un MQ2 saturado
 * (el ADC llegó a su techo) NO es una falla de la sonda: es una lectura válida y alta, de "al menos"
 * MQ2_AO_SATURADO_MV.
 *
 * Parámetro: intervalo_s. Comando propio: {"command":"silence"} calla el buzzer por las causas
 * activas, hasta que se normalicen o pasen ALARMA_SILENCIO_MAX_MS (30 min); sin causa activa se
 * rechaza ("sin_alarma"). El aula la asigna AURA con set_config {"aula": ...}: no está acá.
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
#define BUZZER_PERIODO_MS    50

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
static bool publicar_ya = false;          /* cambió el estado de una causa: publicar sin esperar el intervalo */
static causa_estado_t avisado_humo = CAUSA_NORMAL;      /* último estado informado a AURA de cada causa */
static causa_estado_t avisado_co2 = CAUSA_NORMAL;

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

static int64_t ahora_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool ejecutar_comando(const char *comando, const cJSON *params, bool local, char *motivo, size_t tam)
{
    (void)params;
    if (strcmp(comando, "silence") == 0) {
        xSemaphoreTake(mtx_alarma, portMAX_DELAY);
        bool habia_alarma = alarma_silenciar(&alarma, (uint64_t)ahora_ms());
        if (habia_alarma) {
            buzzer(false);
        }
        xSemaphoreGive(mtx_alarma);
        if (!habia_alarma) {
            /* Sin causa activa no hay nada que callar. Aceptarlo "para después" dejaría mudo el
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

/* Informa cuánto falta de calentamiento. Durante el calentamiento el MQ2 no se evalúa y no se publica,
   pero el SCD40 sí se lee: el aviso de CO2 no espera. */
static void informar_calentamiento(void)
{
    uint32_t restante = calentamiento_restante_s((uint64_t)t_calentamiento_ms, (uint64_t)ahora_ms(), CALENTAMIENTO_S);
    ESP_LOGI(TAG, "calentando el MQ2: faltan %lu s (sin publicar, aviso de humo inhibido; el de CO2 ya funciona)",
             (unsigned long)restante);
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

/* Alerta a AURA por un cambio de estado de una causa (contrato 3.5). */
static void alertar_causa(const char *tipo, const char *nombre, const char *unidad, causa_estado_t estado,
                          const causa_t *c, uint32_t umbral)
{
    const char *nombre_estado = estado == CAUSA_ACTIVA ? "activa" : (estado == CAUSA_SILENCIADA ? "silenciada" : "normalizada");
    char msg[96];
    if (estado == CAUSA_ACTIVA) {
        snprintf(msg, sizeof(msg), "%s: %s%lu %s (umbral %lu %s)", nombre, c->saturado ? "al menos " : "",
                 (unsigned long)c->valor, unidad, (unsigned long)umbral, unidad);
    } else if (estado == CAUSA_SILENCIADA) {
        snprintf(msg, sizeof(msg), "%s: aviso silenciado", nombre);
    } else {
        snprintf(msg, sizeof(msg), "%s: se normalizó (%lu %s)", nombre, (unsigned long)c->valor, unidad);
    }
    cJSON *d = cJSON_CreateObject();
    if (d != NULL) {
        cJSON_AddStringToObject(d, "estado", nombre_estado);
        cJSON_AddNumberToObject(d, "valor", c->valor);
        cJSON_AddNumberToObject(d, "umbral", umbral);
        cJSON_AddBoolToObject(d, "saturado", c->saturado);
    }
    nodo_mesh_alerta(tipo, estado == CAUSA_ACTIVA ? "warning" : "info", msg, d);
    cJSON_Delete(d);
}

/* ---------- Un ciclo de medición local (cada SENSOR_PERIODO_MS) ---------- */
static void ciclo_medicion(bool calentando)
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

    /* El MQ2 recién se lee y se evalúa terminado el calentamiento (el calefactor tarda en estabilizar) */
    uint32_t gas_mv = 0;
    mq2_estado_t gas_est = MQ2_SIN_RESPUESTA;
    bool gas_valido = false, gas_saturado = false;
    if (!calentando) {
        gas_est = mq2_leer(&gas_mv);
        gas_saturado = (gas_est == MQ2_SATURADA);
        if (gas_saturado) {
            gas_mv = MQ2_AO_SATURADO_MV;      /* el valor real es AL MENOS este */
        }
        gas_valido = (gas_est == MQ2_LECTURA_OK) || gas_saturado;   /* saturado es una lectura válida y alta */

        /* Estado de las sondas hacia AURA, con histéresis. Solo con el calentamiento terminado, así
           que una falla informada es una falla real. */
        registrar_sonda(&s_co2, "co2_ppm", co2_ok, co2, t, "sin_respuesta");
        registrar_sonda(&s_temp, "temp_c", co2_ok, temp, t, "sin_respuesta");
        registrar_sonda(&s_hum, "hum_pct", co2_ok, hum, t, "sin_respuesta");
        registrar_sonda(&s_gas, "gas_mv", gas_valido, gas_mv, t, gas_est == MQ2_SIN_RESPUESTA ? "sin_respuesta" : "fuera_de_rango");
        sensor_con_falla = s_co2.en_falla || s_gas.en_falla;
    }

    /* SCD40 mudo varios ciclos seguidos (cable flojo, brownout, quedó trabado): se lo reinicia.
       Vale también si no respondió en el arranque: se reintenta cada tanto hasta que conteste. */
    if (bus_i2c != NULL && sonda_toca_reiniciar(scd4x_ciclos_sin_lectura)) {
        ESP_LOGW(TAG, "SCD40 sin lectura hace %lu ciclos: reiniciando el sensor", (unsigned long)scd4x_ciclos_sin_lectura);
        scd4x_ok = (scd4x_iniciar(bus_i2c) == ESP_OK);
    }

    /* Aviso de ventilación (no depende de la mesh). Solo una lectura válida de ESTE ciclo cambia el
       estado de la causa: una lectura mala suelta no apaga ni prende nada. */
    xSemaphoreTake(mtx_alarma, portMAX_DELAY);
    alarma_actualizar(&alarma, t, gas_valido, gas_mv, gas_saturado, co2_ok, co2);
    causa_t humo = alarma.humo, co2c = alarma.co2;
    xSemaphoreGive(mtx_alarma);

    /* Cada cambio de estado de una causa (incluido un silence, que llega de otra tarea, o el
       vencimiento del silencio) se avisa a AURA y se publica ya, sin esperar el intervalo_s ni
       que la mesh esté arriba en ese instante (la cola de nodo_mesh lo guarda). */
    if (co2c.estado != avisado_co2) {
        avisado_co2 = co2c.estado;
        alertar_causa("dioxido_carbono_alto", "CO2", "ppm", co2c.estado, &co2c, CO2_ALARMA_PPM);
        publicar_ya = true;
    }
    if (humo.estado != avisado_humo) {
        avisado_humo = humo.estado;
        alertar_causa("humo_vapores", "Humo/vapores (MQ2)", "mV", humo.estado, &humo, HUMO_ALARMA_MV);
        publicar_ya = true;
    }

    static const char *const nombre_estado[] = {"normal", "ACTIVA", "silenciada"};
    ESP_LOGI(TAG, "CO2 %s%u ppm | T %.1f C | HR %.1f %% | MQ2 %s%lu mV | aviso: CO2 %s, humo %s",
             co2_ok ? "" : "(sin dato) ", co2, temp, hum,
             calentando ? "(calentando) " : (gas_saturado ? "(saturado, al menos) " : (gas_valido ? "" : "(inválido) ")),
             (unsigned long)gas_mv, nombre_estado[co2c.estado], nombre_estado[humo.estado]);
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

/* Buzzer: un pitido corto cada tanto mientras una causa esté activa (alarma_buzzer). Tarea propia
   porque el pitido dura décimas de segundo y el ciclo de medición es de 5 s. */
static void tarea_buzzer(void *arg)
{
    while (true) {
        xSemaphoreTake(mtx_alarma, portMAX_DELAY);
        buzzer(alarma_buzzer(&alarma, (uint64_t)ahora_ms()));
        xSemaphoreGive(mtx_alarma);
        vTaskDelay(pdMS_TO_TICKS(BUZZER_PERIODO_MS));
    }
}

static void tarea_sensores(void *arg)
{
    TickType_t base = xTaskGetTickCount();
    int64_t t_publicacion_ms = 0;
    bool ya_publico = false;
    while (true) {
        bool calentando = calentamiento_activo((uint64_t)t_calentamiento_ms, (uint64_t)ahora_ms(), CALENTAMIENTO_S);
        if (calentando) {
            informar_calentamiento();
        }
        ciclo_medicion(calentando);
        if (!calentando) {
            /* Terminado el calentamiento se publica enseguida y después cada intervalo_s, siempre
               que haya alguna medición vigente (contrato 3.1), o ya si cambió el estado de una causa.
               Una sola publicación lleva los valores de los dos sensores juntos. */
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
    xTaskCreate(tarea_buzzer, "buzzer", 2048, NULL, 3, NULL);

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
    ESP_LOGI(TAG, "calentando el MQ2 %u s: no se publica ni se evalúa el humo hasta terminar (el aviso de CO2 ya funciona)", CALENTAMIENTO_S);

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
