/*
 * E1-PB-LECA-AMB01 - nodo de gas y CO2 del LabECA (ESP32-C3 SuperMini), hoja de la mesh de AURA.
 * ESP-IDF 6.1 sobre la biblioteca nodo_mesh (Mesh_Red_Completa/componentes/nodo_mesh), contrato v4.1.
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
 * mientras calienta, magenta rápido con la alarma y cian (dos destellos) si falla un sensor.
 *
 * Calentamiento: los dos sensores arrancan juntos y comparten un solo reloj de CALENTAMIENTO_S
 * (2 min). Mientras dura no se publica nada, no se informa el estado de las sondas y la alarma y
 * el buzzer están inhibidos. Tras cualquier reinicio (por ejemplo un corte de luz) vuelve a
 * empezar completo.
 *
 * Dos ritmos distintos, una vez terminado el calentamiento:
 *   - cada SENSOR_PERIODO_MS (5 s) se mide y se evalúa la alarma local.
 *   - cada intervalo_s (set_config, 5..86400 s) se publica la última medición válida.
 *
 * Parámetro: intervalo_s. Comando propio: {"command":"silence"} calla el buzzer hasta que la
 * alarma se normalice. El aula la asigna AURA con set_config {"aula": ...}: no está acá.
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

static bool scd4x_ok = false;
static volatile bool sensor_con_falla = false;          /* algún sensor sin medición válida */
static volatile bool calentamiento_en_marcha = false;   /* true una vez que arrancaron los dos sensores */
static int64_t t_calentamiento_ms;        /* instante (desde el arranque del chip) en que arrancaron los sensores */

/* Última medición válida, para publicarla a su ritmo */
static struct {
    bool co2_ok;
    uint16_t co2;
    double temp, hum;
    bool gas_ok;
    uint32_t gas_mv;
} ult;

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
        alarma_silenciar(&alarma);
        buzzer(false);
        xSemaphoreGive(mtx_alarma);
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

/* ---------- Un ciclo de medición local (cada SENSOR_PERIODO_MS) ---------- */
static void ciclo_medicion(void)
{
    uint16_t co2 = 0;
    double temp = 0, hum = 0;
    bool co2_ok = false;

    if (scd4x_ok) {
        esp_err_t e = scd4x_leer(&co2, &temp, &hum, 1500);
        if (e != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            e = scd4x_leer(&co2, &temp, &hum, 1500);      /* un reintento */
        }
        co2_ok = (e == ESP_OK && co2 != 0);
        if (e != ESP_OK) {
            lecturas_fallidas++;
            ESP_LOGW(TAG, "error leyendo el SCD40: %s", esp_err_to_name(e));
        } else {
            lecturas_ok++;
        }
    }

    uint32_t gas_mv = 0;
    bool gas_ok = mq2_leer(&gas_mv);
    sensor_con_falla = !co2_ok || !gas_ok;

    /* Estado de las sondas hacia AURA: avisa una vez por cambio (contrato 3.5). Solo se llega acá
       con el calentamiento terminado, así que una falla informada es una falla real. */
    nodo_mesh_sonda("co2_ppm", co2_ok, "sin_respuesta");
    nodo_mesh_sonda("temp_c", co2_ok, "sin_respuesta");
    nodo_mesh_sonda("hum_pct", co2_ok, "sin_respuesta");
    nodo_mesh_sonda("gas_mv", gas_ok, "fuera_de_rango");

    /* Última medición válida, para publicarla a su ritmo */
    ult.co2_ok = co2_ok;
    if (co2_ok) {
        ult.co2 = co2;
        ult.temp = temp;
        ult.hum = hum;
    }
    ult.gas_ok = gas_ok;
    if (gas_ok) {
        ult.gas_mv = gas_mv;
    }

    /* Alarma local (no depende de la mesh) */
    xSemaphoreTake(mtx_alarma, portMAX_DELAY);
    bool suena = alarma_actualizar(&alarma, gas_ok, gas_mv, co2_ok, co2);
    buzzer(suena);
    bool en_alarma = alarma.activa;
    bool silenciada = alarma.silenciada;
    xSemaphoreGive(mtx_alarma);

    ESP_LOGI(TAG, "CO2 %s%u ppm | T %.1f C | HR %.1f %% | gas %s%lu mV | alarma %s%s", co2_ok ? "" : "(sin dato) ", co2,
             temp, hum, gas_ok ? "" : "(inválido) ", (unsigned long)gas_mv, en_alarma ? "ACTIVA" : "ok",
             (en_alarma && silenciada) ? " (buzzer silenciado)" : "");
    ESP_LOGI(TAG, "SCD40: %lu lecturas ok, %lu fallidas | mesh: %lu pendientes de confirmar, aula '%s'",
             (unsigned long)lecturas_ok, (unsigned long)lecturas_fallidas, (unsigned long)nodo_mesh_pendientes(),
             nodo_mesh_aula());
}

/* ---------- Publicación a la mesh (cada intervalo_s) ---------- */
static void publicar(void)
{
    cJSON *v = cJSON_CreateObject();
    if (v == NULL) {
        return;
    }
    if (ult.co2_ok) {
        /* double con un decimal: cJSON escribe 24.1 y no 24.100000381 */
        cJSON_AddNumberToObject(v, "co2_ppm", ult.co2);
        cJSON_AddNumberToObject(v, "temp_c", round(ult.temp * 10.0) / 10.0);
        cJSON_AddNumberToObject(v, "hum_pct", round(ult.hum * 10.0) / 10.0);
    }
    if (ult.gas_ok) {
        cJSON_AddNumberToObject(v, "gas_mv", ult.gas_mv);
    }
    /* Si ninguna sonda dio un valor válido, nodo_mesh_medicion no guarda nada (contrato 3.1) */
    nodo_mesh_medicion(v);
    cJSON_Delete(v);
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
            if ((ult.co2_ok || ult.gas_ok) && (!ya_publico || ahora - t_publicacion_ms >= (int64_t)intervalo_s * 1000)) {
                publicar();
                ya_publico = true;
                t_publicacion_ms = ahora;
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
        ESP_LOGE(TAG, "el MQ2 no se pudo iniciar: gas_mv saldrá como sonda con falla");
    }

    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &bus) == ESP_OK) {
        escanear_i2c(bus);
        scd4x_ok = (scd4x_iniciar(bus) == ESP_OK);
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
