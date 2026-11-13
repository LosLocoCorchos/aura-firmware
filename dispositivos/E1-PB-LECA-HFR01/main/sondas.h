#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t hfr_sondas_iniciar(void);
bool hfr_sonda_leer(int indice, float *temperatura);
