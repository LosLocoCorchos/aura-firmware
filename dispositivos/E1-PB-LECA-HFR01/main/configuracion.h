#pragma once
#include "logica.h"
#include "cJSON.h"
#include "esp_err.h"
bool hfr_config_parsear(const cJSON *params, const hfr_config_t *actual,
                       hfr_config_t *siguiente, char *motivo, size_t tam);
void hfr_config_a_json(const hfr_config_t *cfg, cJSON *objeto);
esp_err_t hfr_config_iniciar(bool nvs_disponible);
hfr_config_t hfr_config_actual(void);
bool hfr_config_aplicar(const cJSON *params, char *motivo, size_t tam);
void hfr_config_describir(cJSON *objeto);
