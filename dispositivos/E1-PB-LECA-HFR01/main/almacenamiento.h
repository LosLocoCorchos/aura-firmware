#pragma once
#include "logica.h"
#include "esp_err.h"
esp_err_t hfr_guardar_config(const hfr_config_t *cfg);
esp_err_t hfr_cargar_config(hfr_config_t *cfg);
esp_err_t hfr_guardar_alimentacion(bool bateria);
bool hfr_cargar_alimentacion(bool *bateria);
