#include "bus_i2c.h"
#include "hardware.h"
#include "esp_log.h"

static i2c_master_bus_handle_t bus;

bool hfr_i2c_iniciar(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = HFR_PIN_SDA,
        .scl_io_num = HFR_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE("I2C", "Bus no disponible: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

esp_err_t hfr_i2c_dispositivo(uint8_t direccion, i2c_master_dev_handle_t *dispositivo)
{
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_probe(bus, direccion, 50);
    if (err != ESP_OK) {
        return err;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = direccion,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &cfg, dispositivo);
}
