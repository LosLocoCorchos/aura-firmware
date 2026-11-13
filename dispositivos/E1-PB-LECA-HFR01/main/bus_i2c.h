#pragma once
#include "driver/i2c_master.h"
bool hfr_i2c_iniciar(void);
esp_err_t hfr_i2c_dispositivo(uint8_t direccion, i2c_master_dev_handle_t *dispositivo);
