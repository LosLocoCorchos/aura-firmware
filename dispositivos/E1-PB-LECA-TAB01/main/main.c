/*
 * AURA - E1-PB-LECA-TAB01: medidor de energía del tablero del LabECA.
 * Hoja de la mesh (ESP32-C3 SuperMini, ESP-IDF 6.1) sobre la biblioteca nodo_mesh.
 *
 * Lo propio del dispositivo:
 *  - Mide cada "intervalo_s" el Schneider PM2130 por Modbus RTU (RS-485) y manda en values:
 *      {"v_an_v":233.58,"i_a_a":1.8,"p_w":328.5,"cosphi_a":0.83}
 *    con los mismos nombres y rangos que conectores/E1-PB-LECA-TAB01/conector.toml.
 *  - Un valor que no se pudo leer o que está fuera de rango NO va en values; la falla de un
 *    campo obligatorio se informa con nodo_mesh_sonda (alerta "sensor", solo cuando cambia).
 *  - set_config: "intervalo_s" (2 a 3600 s) e "id_esclavo" (1 a 247). Se valida todo antes de
 *    aplicar y se guarda en NVS. "aula" la maneja la biblioteca.
 *  - No acepta comandos propios: la biblioteca contesta "comando_desconocido".
 *
 * La lógica del medidor (trama, validación, conversión, rangos) está en pm2130.c y se prueba
 * en la PC con tests/ (make -C tests).
 *
 * Conexión del módulo MAX485 (DE y RE puenteados):
 *   DI <- GPIO21   RO -> GPIO20   DE+RE <- GPIO5   VCC 3V3   GND
 *   A -> D1 del PM2130, B -> D0, GND -> 0V.  Modbus: esclavo 1, 19200 baudios, 8E1.
 *
 * MESH_ID y MESH_AP_PASS: en main/config_local.h (los da la cátedra; no se sube al repo).
 * Todas las placas de este dispositivo llevan el MISMO binario: no hay MAC, id ni aula.
 *
 * Tipo y versión (CMakeLists.txt): aura-energia-pm2130, PROJECT_VER. Se actualiza por la malla
 * (OTA); la imagen se firma aparte antes de grabarla o mandarla (GUIA_NODOS_HOJA.md, 2.9).
 * Al arrancar informa por serie su tipo y su versión. Para ver a simple vista que una OTA quedó
 * instalada, MOSTRAR_LED_AL_MEDIR enciende el LED azul de la placa en cada medición.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "aura_trama.h"                   /* AURA_VALUES_MAX */
#include "cJSON.h"
#include "nodo_mesh.h"
#include "pm2130.h"

#if __has_include("config_local.h")
#include "config_local.h"
#else
#error "Falta main/config_local.h: copiá config_local.ejemplo.h como config_local.h y completá el ID y la clave de la mesh"
#endif

/* Prueba de OTA: compilar la 1.0.0 con 0 y la 1.0.1 con 1. Si después de la actualización el LED
 * azul de la placa parpadea en cada medición, quedó instalada la versión nueva. */
#define MOSTRAR_LED_AL_MEDIR 0
#define PIN_LED             GPIO_NUM_8   /* LED azul de la C3 SuperMini */
#define LED_ACTIVO_BAJO     1            /* en la SuperMini se enciende con nivel bajo */

/* Potencia en pasos de 0,25 dBm: la de la plantilla de la cátedra para la C3 SuperMini. */
#define POTENCIA_TX         60

#define INTERVALO_DEFECTO_S 10
#define INTERVALO_MIN_S     2
#define INTERVALO_MAX_S     3600
#define ESCLAVO_DEFECTO     1
#define ESCLAVO_MIN         1
#define ESCLAVO_MAX         247


/* ---- RS-485 ---- */
#define MB_UART             UART_NUM_1
#define MB_TX_PIN           21           /* -> DI */
#define MB_RX_PIN           20           /* <- RO */
#define MB_DIR_PIN          5            /* -> DE + RE: alto = transmitir */
#define MB_BAUDIOS          19200
#define MB_TIMEOUT_MS       300
#define MB_REINTENTOS       1
#define MB_PAUSA_MS         30

static const char *TAG = "TAB01";
static volatile uint32_t intervalo_s = INTERVALO_DEFECTO_S;
static TaskHandle_t tarea_medicion_h;   /* para avisarle un cambio de intervalo */
static volatile uint8_t id_esclavo = ESCLAVO_DEFECTO;

/* ============================ RS-485 / Modbus ============================ */

static void modbus_iniciar(void)
{
    const uart_config_t cfg = {
        .baud_rate = MB_BAUDIOS,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,              /* 8E1: lo de fábrica del PM2130 */
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(MB_UART, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MB_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MB_UART, MB_TX_PIN, MB_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    const gpio_config_t dir = {
        .pin_bit_mask = 1ULL << MB_DIR_PIN,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&dir));
    gpio_set_level(MB_DIR_PIN, 0);               /* arranca escuchando */
}

static uint8_t modbus_leer_una_vez(uint16_t registro, uint8_t cantidad, uint16_t *destino)
{
    uint8_t tx[PM_LARGO_CONSULTA];
    pm_armar_consulta(id_esclavo, registro, cantidad, tx);

    uart_flush_input(MB_UART);
    gpio_set_level(MB_DIR_PIN, 1);               /* transmitir */
    uart_write_bytes(MB_UART, tx, sizeof(tx));
    uart_wait_tx_done(MB_UART, pdMS_TO_TICKS(100));   /* hasta que sale el último bit */
    gpio_set_level(MB_DIR_PIN, 0);               /* escuchar */

    uint8_t rx[5 + 2 * 8];
    int n = uart_read_bytes(MB_UART, rx, 3, pdMS_TO_TICKS(MB_TIMEOUT_MS));
    if (n < 3) return PM_ERR_SIN_RESPUESTA;
    size_t largo = pm_largo_respuesta(rx);
    if (largo > sizeof(rx)) return PM_ERR_TRAMA;
    n += uart_read_bytes(MB_UART, rx + 3, largo - 3, pdMS_TO_TICKS(MB_TIMEOUT_MS));
    return pm_validar_respuesta(id_esclavo, cantidad, rx, (size_t)n, destino);
}

static uint8_t modbus_leer(uint16_t registro, uint8_t cantidad, uint16_t *destino)
{
    uint8_t r = PM_ERR_SIN_RESPUESTA;
    for (int intento = 0; intento <= MB_REINTENTOS; intento++) {
        r = modbus_leer_una_vez(registro, cantidad, destino);
        vTaskDelay(pdMS_TO_TICKS(MB_PAUSA_MS));
        if (r == PM_OK) break;
    }
    return r;
}

/* ================================ Medición ================================ */

/* Agrega a "todo" una copia de cada campo de "parte". */
static void sumar(cJSON *todo, const cJSON *parte)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, parte) {
        cJSON_AddItemToObject(todo, item->string, cJSON_Duplicate(item, true));
    }
}

static size_t largo_json(const cJSON *obj)
{
    char *s = cJSON_PrintUnformatted(obj);
    size_t n = s ? strlen(s) : 0;
    cJSON_free(s);
    return n;
}

static void guardar(const char *que, const cJSON *values)
{
    char *s = cJSON_PrintUnformatted(values);
    bool ok = nodo_mesh_medicion(values);
    ESP_LOGI(TAG, "%s %s: %s", que, ok ? "guardada" : "NO guardada", s ? s : "");
    cJSON_free(s);
}

static void medir_y_guardar(void)
{
    cJSON *principal = cJSON_CreateObject();     /* obligatorios y fases B y C */
    cJSON *extras = cJSON_CreateObject();        /* opcionales */
    bool mudo = false;                           /* el medidor no contestó: no se insiste */
    double p_w = NAN;

    for (size_t i = 0; i < PM_N_CAMPOS; i++) {
        const pm_campo_t *c = &PM_CAMPOS[i];
        uint16_t w[2];
        uint8_t r = mudo ? PM_ERR_SIN_RESPUESTA : modbus_leer(c->registro, 2, w);
        if (r == PM_ERR_SIN_RESPUESTA) mudo = true;

        double v = 0;
        bool leido = (r == PM_OK) && pm_convertir(c, w, &v);
        bool valido = leido && pm_en_rango(c, v);
        if (c->obligatorio) {
            nodo_mesh_sonda(c->nombre, valido, leido ? "fuera_de_rango" : "sin_respuesta");
        }
        if (!valido) {
            if (r != PM_OK && r != PM_ERR_SIN_RESPUESTA) ESP_LOGW(TAG, "%s: error 0x%02X", c->nombre, r);
            continue;
        }
        if (strcmp(c->nombre, "p_w") == 0) p_w = v;
        bool es_extra = !c->obligatorio && strncmp(c->nombre, "thd_", 4) == 0;
        cJSON_AddNumberToObject(es_extra ? extras : principal, c->nombre, v);
    }

#if ENVIAR_TANFI
    if (!mudo && !isnan(p_w) && fabs(p_w) >= PM_P_MIN_TANFI_W) {
        uint16_t w[2];
        float q_kvar;
        if (modbus_leer(PM_REG_Q_TOTAL, 2, w) == PM_OK && pm_a_float(w, &q_kvar)) {
            double tan_phi = pm_redondear((double)q_kvar * 1000.0 / p_w, 2);   /* + inductivo */
            if (tan_phi >= PM_TANFI_MIN && tan_phi <= PM_TANFI_MAX) cJSON_AddNumberToObject(extras, "tanphi", tan_phi);
        }
    }
#endif
#if ENVIAR_ENERGIA
    if (!mudo) {
        uint16_t w[4];
        int64_t wh;
        if (modbus_leer(PM_REG_ENERGIA, 4, w) == PM_OK && pm_a_int64(w, &wh)) {
            double kwh = pm_redondear((double)wh / 1000.0, 3);
            if (kwh >= 0 && kwh <= PM_ENERGIA_MAX_KWH) cJSON_AddNumberToObject(extras, "e_act_kwh", kwh);
        }
    }
#endif
    (void)p_w;

    if (cJSON_GetArraySize(principal) + cJSON_GetArraySize(extras) == 0) {
        ESP_LOGW(TAG, "Sin mediciones validas (%s): no se guarda nada",
                 mudo ? "el medidor no contesta" : "valores fuera de rango");
    } else {
        cJSON *todo = cJSON_CreateObject();
        sumar(todo, principal);
        sumar(todo, extras);
        if (largo_json(todo) <= AURA_VALUES_MAX) {
            guardar("Medicion", todo);
        } else {
            /* No entra en una trama: dos mediciones (contrato §3.1: values no trae todos los campos) */
            if (cJSON_GetArraySize(principal)) guardar("Medicion (1/2)", principal);
            if (cJSON_GetArraySize(extras))    guardar("Medicion (2/2)", extras);
        }
        cJSON_Delete(todo);
    }
    cJSON_Delete(principal);
    cJSON_Delete(extras);
}

static void led(bool encendido)
{
    gpio_set_level(PIN_LED, LED_ACTIVO_BAJO ? !encendido : encendido);
}

static void tarea_medicion(void *arg)
{
    while (true) {
        medir_y_guardar();
#if MOSTRAR_LED_AL_MEDIR
        led(true);                               /* la versión con el cambio visible */
        vTaskDelay(pdMS_TO_TICKS(200));
        led(false);
#endif
        /* Espera el intervalo, o menos si llega uno nuevo por set_config: en ese caso mide
         * enseguida y sigue con el nuevo, en lugar de esperar lo que quedaba del anterior. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(intervalo_s * 1000));
    }
}

/* ========================= set_config (contrato §3.3) ========================= */

static bool entero_en_rango(const cJSON *p, double min, double max)
{
    return cJSON_IsNumber(p) && p->valuedouble == floor(p->valuedouble) &&
           p->valuedouble >= min && p->valuedouble <= max;
}

/* Valida TODO antes de aplicar algo. "aula" la maneja la biblioteca. */
static bool aplicar_config(const cJSON *params, char *motivo, size_t tam)
{
    const cJSON *p;
    cJSON_ArrayForEach(p, params) {
        if (strcmp(p->string, "intervalo_s") != 0 && strcmp(p->string, "id_esclavo") != 0 &&
            strcmp(p->string, "aula") != 0) {
            snprintf(motivo, tam, "parametro_desconocido");
            return false;
        }
    }
    const cJSON *iv = cJSON_GetObjectItemCaseSensitive(params, "intervalo_s");
    if (iv != NULL && !entero_en_rango(iv, INTERVALO_MIN_S, INTERVALO_MAX_S)) {
        snprintf(motivo, tam, "intervalo_s_fuera_de_rango");
        return false;
    }
    const cJSON *es = cJSON_GetObjectItemCaseSensitive(params, "id_esclavo");
    if (es != NULL && !entero_en_rango(es, ESCLAVO_MIN, ESCLAVO_MAX)) {
        snprintf(motivo, tam, "id_esclavo_fuera_de_rango");
        return false;
    }

    if (iv == NULL && es == NULL) return true;    /* solo aula: la maneja la biblioteca */

    /* Todo validó: primero se guarda y, solo si se pudo, se aplica. Si el NVS falla no cambia
     * nada y se contesta error_de_nvs: lo que responde la placa es lo que va a tener después de
     * un reinicio. */
    nvs_handle_t h;
    if (nvs_open("tab01", NVS_READWRITE, &h) != ESP_OK) {
        snprintf(motivo, tam, "error_de_nvs");
        return false;
    }
    esp_err_t err = ESP_OK;
    if (iv != NULL) err = nvs_set_u32(h, "intervalo_s", (uint32_t)iv->valuedouble);
    if (err == ESP_OK && es != NULL) err = nvs_set_u8(h, "id_esclavo", (uint8_t)es->valuedouble);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        snprintf(motivo, tam, "error_de_nvs");
        return false;
    }

    bool intervalo_cambio = false;
    if (iv != NULL) {
        intervalo_cambio = (uint32_t)iv->valuedouble != intervalo_s;
        intervalo_s = (uint32_t)iv->valuedouble;
    }
    if (es != NULL) id_esclavo = (uint8_t)es->valuedouble;
    ESP_LOGI(TAG, "Configuracion: intervalo %lu s, esclavo %u", (unsigned long)intervalo_s, id_esclavo);
    if (intervalo_cambio && tarea_medicion_h != NULL) xTaskNotifyGive(tarea_medicion_h);
    return true;
}

/* La configuración vigente. "intervalo_s" no puede faltar: con él la raíz infiere offline. */
static void describir_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "intervalo_s", intervalo_s);
    cJSON_AddNumberToObject(config, "id_esclavo", id_esclavo);
}

/* ================================== Inicio ================================== */

static void leer_config_guardada(void)
{
    nvs_handle_t h;
    if (nvs_open("tab01", NVS_READONLY, &h) != ESP_OK) return;
    uint32_t iv;
    if (nvs_get_u32(h, "intervalo_s", &iv) == ESP_OK && iv >= INTERVALO_MIN_S && iv <= INTERVALO_MAX_S) {
        intervalo_s = iv;
    }
    uint8_t es;
    if (nvs_get_u8(h, "id_esclavo", &es) == ESP_OK && es >= ESCLAVO_MIN && es <= ESCLAVO_MAX) {
        id_esclavo = es;
    }
    nvs_close(h);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Firmware %s, version %s (LED al medir: %s)", app->project_name, app->version,
             MOSTRAR_LED_AL_MEDIR ? "si" : "no");

    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    led(false);

    leer_config_guardada();
    modbus_iniciar();
    ESP_LOGI(TAG, "PM2130 por RS-485: TX GPIO%d, RX GPIO%d, DE/RE GPIO%d, esclavo %u, %d 8E1, cada %lu s",
             MB_TX_PIN, MB_RX_PIN, MB_DIR_PIN, id_esclavo, MB_BAUDIOS, (unsigned long)intervalo_s);

    nodo_mesh_config_t cfg = {
        .mesh_id = {MESH_ID_BYTES},
        .mesh_clave = MESH_AP_PASS,
        .potencia_tx = POTENCIA_TX,
        .alimentacion = "red",
        .aplicar_config = aplicar_config,
        .describir_config = describir_config,
        .ejecutar_comando = NULL,               /* sin comandos propios */
    };
    ESP_ERROR_CHECK(nodo_mesh_iniciar(&cfg));
    xTaskCreate(tarea_medicion, "medicion", 4096, NULL, 4, &tarea_medicion_h);
}
