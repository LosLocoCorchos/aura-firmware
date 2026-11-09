#include "scd4x.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensirion.h"

#define SCD4X_DIR          0x62
#define SCD4X_SCL_HZ       50000     /* el bus corre lento, como en la versión de Arduino */
#define I2C_TIMEOUT_MS     200

#define CMD_START_PERIODICO   0x21B1
#define CMD_LEER_MEDICION     0xEC05
#define CMD_STOP_PERIODICO    0x3F86
#define CMD_ESTADO_DATOS      0xE4B8
#define CMD_WAKE_UP           0x36F6
#define CMD_REINIT            0x3646
#define CMD_NUMERO_SERIE      0x3682

static const char *TAG = "SCD40";
static i2c_master_dev_handle_t dev;       /* NULL hasta el primer scd4x_iniciar */

static void esperar_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1);
}

static esp_err_t enviar(uint16_t cmd)
{
    uint8_t b[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF)};
    return i2c_master_transmit(dev, b, sizeof(b), I2C_TIMEOUT_MS);
}

/* Manda un comando, espera lo que pide la hoja de datos y lee la respuesta. */
static esp_err_t leer_respuesta(uint16_t cmd, uint8_t *rx, size_t n, uint32_t demora_ms)
{
    esp_err_t e = enviar(cmd);
    if (e != ESP_OK) {
        return e;
    }
    esperar_ms(demora_ms);
    return i2c_master_receive(dev, rx, n, I2C_TIMEOUT_MS);
}

esp_err_t scd4x_iniciar(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD4X_DIR,
        .scl_speed_hz = SCD4X_SCL_HZ,
    };
    esp_err_t e;
    if (dev == NULL) {
        /* El dispositivo se agrega al bus una sola vez: scd4x_iniciar también se llama para
           reiniciar el sensor tras una falla, y cada alta de más gastaría un handle del bus. */
        e = i2c_master_bus_add_device(bus, &cfg, &dev);
        if (e != ESP_OK) {
            dev = NULL;
            ESP_LOGE(TAG, "no pude agregar el SCD40 al bus: %s", esp_err_to_name(e));
            return e;
        }
    }

    /* Arranque limpio, respetando el tiempo de cada comando. El wake_up solo existe en el
       SCD41: en el SCD40 no contesta, y no importa. */
    enviar(CMD_WAKE_UP);
    esperar_ms(30);
    e = enviar(CMD_STOP_PERIODICO);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "stop_periodic_measurement: %s", esp_err_to_name(e));
    }
    esperar_ms(500);
    e = enviar(CMD_REINIT);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "reinit: %s", esp_err_to_name(e));
    }
    esperar_ms(30);

    uint8_t rx[9];
    e = leer_respuesta(CMD_NUMERO_SERIE, rx, sizeof(rx), 2);
    uint16_t s0, s1, s2;
    if (e == ESP_OK && sensirion_palabra(rx, &s0) && sensirion_palabra(rx + 3, &s1) && sensirion_palabra(rx + 6, &s2)) {
        ESP_LOGI(TAG, "número de serie 0x%04X%04X%04X", s0, s1, s2);
    } else {
        ESP_LOGW(TAG, "no pude leer el número de serie");
    }

    e = enviar(CMD_START_PERIODICO);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "no pude iniciar la medición: %s", esp_err_to_name(e));
        return e;
    }
    ESP_LOGI(TAG, "midiendo cada 5 s");
    return ESP_OK;
}

esp_err_t scd4x_leer(uint16_t *co2_ppm, double *temp_c, double *hum_pct, uint32_t espera_ms)
{
    for (;;) {
        uint8_t rx3[3];
        uint16_t estado;
        esp_err_t e = leer_respuesta(CMD_ESTADO_DATOS, rx3, sizeof(rx3), 2);
        if (e != ESP_OK) {
            return e;
        }
        if (!sensirion_palabra(rx3, &estado)) {
            return ESP_ERR_INVALID_CRC;
        }
        if ((estado & 0x07FF) != 0) {          /* hay una medición nueva */
            break;
        }
        if (espera_ms == 0) {
            return ESP_ERR_NOT_FOUND;
        }
        uint32_t paso = espera_ms < 100 ? espera_ms : 100;
        esperar_ms(paso);
        espera_ms -= paso;
    }

    uint8_t rx[9];
    esp_err_t e = leer_respuesta(CMD_LEER_MEDICION, rx, sizeof(rx), 2);
    if (e != ESP_OK) {
        return e;
    }
    return scd4x_decodificar(rx, co2_ppm, temp_c, hum_pct) ? ESP_OK : ESP_ERR_INVALID_CRC;
}
