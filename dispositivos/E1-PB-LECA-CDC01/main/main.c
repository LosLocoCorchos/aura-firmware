/*
 * Hoja de CO2 de la mesh de AURA (ESP32-C3 SuperMini) con sensor MH-Z19C y 3 LEDs.
 * Sobre la biblioteca nodo_mesh: lo propio del dispositivo es solo esto (qué mide, qué
 * parámetros tiene y qué comandos acepta). La mesh, la cola, el ack y la hora los resuelve
 * nodo_mesh.
 *
 *  - Mide co2_ppm cada "intervalo_s" y lo manda en values. Nada más: la temperatura del
 *    MH-Z19C es interna del módulo y no es una temperatura ambiente confiable.
 *  - Primeros 3 minutos: el sensor se calienta. No se publica nada y el LED amarillo parpadea.
 *  - Si el sensor no responde o da un valor fuera de rango: no se manda el campo (contrato
 *    §3.1) y se avisa con nodo_mesh_sonda() (alerta "sensor", una sola vez por cambio).
 *  - LEDs locales: verde hasta atencion_ppm, amarillo hasta alto_ppm, rojo por encima.
 *    La alerta por umbral hacia AURA la calcula el conector en Python, no la placa.
 *  - Zumbador en GPIO10 (módulo activo en bajo, suena con 0 V): 3 pitidos al pasar a rojo
 *    (CO2 > alto_ppm), que se repiten mientras siga en rojo cuando pasaron 60 s o más (se mira
 *    en cada medición, así que depende de intervalo_s). En cada arranque suenan además 2 pitidos
 *    al terminar el calentamiento.
 *    sonido = 0 lo silencia y queda guardado en NVS hasta que set_config lo cambie.
 *  - Parámetros (set_config, validados y guardados en NVS): intervalo_s, atencion_ppm, alto_ppm,
 *    sonido (0 o 1).
 *
 * Conexiones (por número de GPIO):
 *   MH-Z19C rojo  (Vin) -> 5V          MH-Z19C negro (GND) -> GND
 *   MH-Z19C verde (TX)  -> GPIO3 (RX de la placa)
 *   MH-Z19C azul  (RX)  -> GPIO4 (TX de la placa)
 *   LED verde -> GPIO7, amarillo -> GPIO6, rojo -> GPIO5 (cada uno con 220 ohm a GND)
 *   Zumbador (módulo activo en bajo): VCC -> 3V3, I/O -> GPIO10, GND -> GND (con 5 V no se apaga)
 *
 * MESH_ID y MESH_AP_PASS: en config_local.h (igual que en el gateway y el concentrador).
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "cJSON.h"
#include "nodo_mesh.h"

#if __has_include("config_local.h")
#include "config_local.h"
#else
#error "Falta main/config_local.h: copiá config_local.ejemplo.h como config_local.h y completá el ID y la clave de la mesh"
#endif

/* ---------- Pines y sensor ---------- */
#define SENSOR_UART         UART_NUM_1
#define SENSOR_RX_PIN       GPIO_NUM_3        /* <- TX del sensor (verde) */
#define SENSOR_TX_PIN       GPIO_NUM_4        /* -> RX del sensor (azul) */
#define PIN_LED_VERDE       GPIO_NUM_7
#define PIN_LED_AMARILLO    GPIO_NUM_6
#define PIN_LED_ROJO        GPIO_NUM_5
#define PIN_ZUMBADOR        GPIO_NUM_10       /* módulo activo en bajo: suena con el pin en 0 V */
#define ZUMBADOR_ACTIVO_EN_BAJO 1         /* 1 = nuestro módulo (low level trigger); con uno activo en alto, poné 0 */
#define ZUMBADOR_ON   (ZUMBADOR_ACTIVO_EN_BAJO ? 0 : 1)
#define ZUMBADOR_OFF  (ZUMBADOR_ACTIVO_EN_BAJO ? 1 : 0)
#define AVISO_REPETIR_MS    60000             /* mientras siga en rojo, vuelve a sonar cada 60 s */
#define PITIDOS_POR_AVISO   3
#define AVISO_LISTO_PITIDOS 2                 /* "listo": termina el precalentamiento */

#define PRECALENTAMIENTO_MS 180000            /* el MH-Z19C necesita ~3 min */
#define CO2_MIN_PPM         400
#define CO2_MAX_PPM         5000              /* rango del MH-Z19C estándar */

/* Potencia en pasos de 0,25 dBm. La C3 SuperMini tiene la antena mal adaptada: ver la guía
   (sección 6). 60 = 15 dBm, con antena de alambre de 31 mm. */
#define POTENCIA_TX         60

/* ---------- Parámetros configurables ---------- */
#define INTERVALO_DEFECTO_S 10
#define INTERVALO_MIN_S     5
#define INTERVALO_MAX_S     3600
#define ATENCION_DEFECTO    1000
#define ALTO_DEFECTO        1500
#define UMBRAL_MIN_PPM      400
#define UMBRAL_MAX_PPM      5000
#define SONIDO_DEFECTO      1

static const char *TAG = "CO2";
static volatile uint32_t intervalo_s  = INTERVALO_DEFECTO_S;
static volatile uint32_t atencion_ppm = ATENCION_DEFECTO;
static volatile uint32_t alto_ppm     = ALTO_DEFECTO;
static volatile uint32_t sonido       = SONIDO_DEFECTO;

/* Comandos del MH-Z19: lectura de CO2 y calibración automática (ABC) apagada.
   Checksum = 0xFF - (suma de los bytes 1 a 7) + 1. */
static const uint8_t CMD_LEER[9]   = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
static const uint8_t CMD_ABC_OFF[9] = {0xFF, 0x01, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86};

/* ---------- Sensor ---------- */
static void sensor_iniciar(void)
{
    const uart_config_t cfg = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(SENSOR_UART, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SENSOR_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(SENSOR_UART, SENSOR_TX_PIN, SENSOR_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

/* Apaga la calibración automática (ABC): en un aula, que no está 24 h seguidas con aire
   exterior, da lecturas bajas. Se manda con el sensor ya caliente. El manual del MH-Z19C dice
   que este comando no devuelve respuesta ("No return value") y no tiene un comando para leer
   el estado del ABC, así que no se puede confirmar leyendo: como es idempotente, se manda
   varias veces para que no se pierda. */
static void sensor_abc_apagar(void)
{
    for (int i = 0; i < 3; i++) {
        uart_write_bytes(SENSOR_UART, CMD_ABC_OFF, sizeof(CMD_ABC_OFF));
        uart_wait_tx_done(SENSOR_UART, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "Calibración automática (ABC) apagada: comando enviado 3 veces");
}

/* Devuelve el CO2 en ppm, o -1 si no hubo respuesta, la trama es corta o el checksum no cierra. */
static int sensor_leer_co2(void)
{
    uint8_t resp[9];
    uart_flush_input(SENSOR_UART);
    uart_write_bytes(SENSOR_UART, CMD_LEER, sizeof(CMD_LEER));
    int n = uart_read_bytes(SENSOR_UART, resp, sizeof(resp), pdMS_TO_TICKS(1000));
    if (n != (int)sizeof(resp)) {
        return -1;
    }
    if (resp[0] != 0xFF || resp[1] != 0x86) {
        return -1;
    }
    uint8_t suma = 0;
    for (int i = 1; i < 8; i++) {
        suma += resp[i];
    }
    if ((uint8_t)(0xFF - suma + 1) != resp[8]) {
        return -1;
    }
    return resp[2] * 256 + resp[3];
}

/* ---------- LEDs ---------- */
static void leds_poner(bool verde, bool amarillo, bool rojo)
{
    gpio_set_level(PIN_LED_VERDE, verde);
    gpio_set_level(PIN_LED_AMARILLO, amarillo);
    gpio_set_level(PIN_LED_ROJO, rojo);
}

static void leds_segun_co2(int co2)
{
    leds_poner(co2 <= (int)atencion_ppm,
               co2 > (int)atencion_ppm && co2 <= (int)alto_ppm,
               co2 > (int)alto_ppm);
}

/* ---------- Zumbador ---------- */
static bool en_rojo = false;
static TickType_t ultimo_aviso = 0;

static void zumbador_pitar(int veces)
{
    for (int i = 0; i < veces; i++) {
        gpio_set_level(PIN_ZUMBADOR, ZUMBADOR_ON);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(PIN_ZUMBADOR, ZUMBADOR_OFF);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

/* Suena al pasar a rojo y se repite cada AVISO_REPETIR_MS mientras siga en rojo.
   Con co2 < 0 (sensor sin lectura) se reinicia el estado, sin sonar. */
static void zumbador_segun_co2(int co2)
{
    bool rojo = (co2 >= 0) && (co2 > (int)alto_ppm);
    TickType_t ahora = xTaskGetTickCount();
    if (rojo && sonido && (!en_rojo || (ahora - ultimo_aviso) >= pdMS_TO_TICKS(AVISO_REPETIR_MS))) {
        zumbador_pitar(PITIDOS_POR_AVISO);
        ultimo_aviso = ahora;
    }
    en_rojo = rojo;
}

/* ---------- Lo que aporta el dispositivo a nodo_mesh ---------- */
static bool leer_param(const cJSON *params, const char *nombre, uint32_t min, uint32_t max,
                       uint32_t *valor, bool *presente, char *motivo, size_t tam)
{
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(params, nombre);
    *presente = (p != NULL);
    if (p == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(p) || p->valuedouble < min || p->valuedouble > max) {
        snprintf(motivo, tam, "%s_fuera_de_rango", nombre);
        return false;
    }
    *valor = (uint32_t)p->valuedouble;
    return true;
}

static bool aplicar_config(const cJSON *params, char *motivo, size_t tam)
{
    /* Primero se valida TODO; solo después se aplica (contrato §3.3) */
    const cJSON *p;
    cJSON_ArrayForEach(p, params) {
        if (strcmp(p->string, "intervalo_s") != 0 && strcmp(p->string, "atencion_ppm") != 0 &&
            strcmp(p->string, "alto_ppm") != 0 && strcmp(p->string, "sonido") != 0 &&
            strcmp(p->string, "aula") != 0) {
            snprintf(motivo, tam, "parametro_desconocido");
            return false;
        }
    }

    uint32_t iv = intervalo_s, at = atencion_ppm, al = alto_ppm, so = sonido;
    bool hay_iv, hay_at, hay_al, hay_so;
    if (!leer_param(params, "intervalo_s", INTERVALO_MIN_S, INTERVALO_MAX_S, &iv, &hay_iv, motivo, tam) ||
        !leer_param(params, "atencion_ppm", UMBRAL_MIN_PPM, UMBRAL_MAX_PPM, &at, &hay_at, motivo, tam) ||
        !leer_param(params, "alto_ppm", UMBRAL_MIN_PPM, UMBRAL_MAX_PPM, &al, &hay_al, motivo, tam) ||
        !leer_param(params, "sonido", 0, 1, &so, &hay_so, motivo, tam)) {
        return false;
    }
    if (at >= al) {                        /* se mira el resultado final, no solo lo que llegó */
        snprintf(motivo, tam, "atencion_debe_ser_menor_que_alto");
        return false;
    }

    intervalo_s = iv;
    atencion_ppm = at;
    alto_ppm = al;
    sonido = so;
    if (hay_iv || hay_at || hay_al || hay_so) {
        nvs_handle_t h;
        if (nvs_open("co2", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u32(h, "intervalo_s", iv);
            nvs_set_u32(h, "atencion_ppm", at);
            nvs_set_u32(h, "alto_ppm", al);
            nvs_set_u32(h, "sonido", so);
            nvs_commit(h);
            nvs_close(h);
        }
        ESP_LOGI(TAG, "Config: intervalo %lu s, atencion %lu ppm, alto %lu ppm, sonido %lu",
                 (unsigned long)iv, (unsigned long)at, (unsigned long)al, (unsigned long)so);
    }
    return true;
}

static void describir_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "intervalo_s", intervalo_s);
    cJSON_AddNumberToObject(config, "atencion_ppm", atencion_ppm);
    cJSON_AddNumberToObject(config, "alto_ppm", alto_ppm);
    cJSON_AddNumberToObject(config, "sonido", sonido);
}

static bool ejecutar_comando(const char *comando, const cJSON *params, bool local, char *motivo, size_t tam)
{
    /* Este dispositivo no tiene actuadores: no acepta comandos propios */
    snprintf(motivo, tam, "comando_desconocido");
    return false;
}

/* ---------- Medición ---------- */
static uint32_t ms_desde_arranque(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static void tarea_medicion(void *arg)
{
    /* Precalentamiento: amarillo parpadeando, no se publica nada */
    while (ms_desde_arranque() < PRECALENTAMIENTO_MS) {
        leds_poner(false, (ms_desde_arranque() / 500) % 2, false);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    leds_poner(false, false, false);
    sensor_abc_apagar();   /* con el sensor ya caliente */
    if (sonido) {
        zumbador_pitar(AVISO_LISTO_PITIDOS);   /* 2 pitidos: el sensor ya está caliente y empieza a medir */
    }

    while (true) {
        int co2 = sensor_leer_co2();
        if (co2 < 0) {
            nodo_mesh_sonda("co2_ppm", false, "sin_respuesta");
            leds_poner(false, false, false);
            zumbador_segun_co2(-1);
            ESP_LOGW(TAG, "El MH-Z19C no responde: revisar cables TX/RX y los 5V");
        } else if (co2 < CO2_MIN_PPM || co2 > CO2_MAX_PPM) {
            nodo_mesh_sonda("co2_ppm", false, "fuera_de_rango");
            leds_poner(false, false, false);
            zumbador_segun_co2(-1);
            ESP_LOGW(TAG, "CO2 fuera de rango: %d ppm", co2);
        } else {
            nodo_mesh_sonda("co2_ppm", true, NULL);
            leds_segun_co2(co2);
            cJSON *v = cJSON_CreateObject();
            cJSON_AddNumberToObject(v, "co2_ppm", co2);
            nodo_mesh_medicion(v);
            cJSON_Delete(v);
            ESP_LOGI(TAG, "CO2: %d ppm", co2);
            zumbador_segun_co2(co2);
        }
        /* Se espera en tramos cortos: si set_config cambia el intervalo, se nota enseguida */
        TickType_t inicio = xTaskGetTickCount();
        while ((xTaskGetTickCount() - inicio) < pdMS_TO_TICKS(intervalo_s * 1000)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
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

    const gpio_num_t leds[] = {PIN_LED_VERDE, PIN_LED_AMARILLO, PIN_LED_ROJO};
    for (int i = 0; i < 3; i++) {
        gpio_reset_pin(leds[i]);
        gpio_set_direction(leds[i], GPIO_MODE_OUTPUT);
    }
    leds_poner(false, false, false);

    gpio_reset_pin(PIN_ZUMBADOR);
    gpio_set_direction(PIN_ZUMBADOR, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_ZUMBADOR, ZUMBADOR_OFF);

    /* Configuración guardada: solo se usa si los tres valores siguen siendo válidos juntos */
    nvs_handle_t h;
    if (nvs_open("co2", NVS_READONLY, &h) == ESP_OK) {
        uint32_t iv, at, al;
        if (nvs_get_u32(h, "intervalo_s", &iv) == ESP_OK && nvs_get_u32(h, "atencion_ppm", &at) == ESP_OK &&
            nvs_get_u32(h, "alto_ppm", &al) == ESP_OK &&
            iv >= INTERVALO_MIN_S && iv <= INTERVALO_MAX_S &&
            at >= UMBRAL_MIN_PPM && al <= UMBRAL_MAX_PPM && at < al) {
            intervalo_s = iv;
            atencion_ppm = at;
            alto_ppm = al;
        }
        uint32_t so;
        if (nvs_get_u32(h, "sonido", &so) == ESP_OK && so <= 1) {
            sonido = so;
        }
        nvs_close(h);
    }

    sensor_iniciar();

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
    xTaskCreate(tarea_medicion, "medicion", 4096, NULL, 4, NULL);
}
