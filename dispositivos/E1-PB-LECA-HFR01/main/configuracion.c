#include "configuracion.h"
#include "almacenamiento.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static SemaphoreHandle_t mutex;
static hfr_config_t vigente;
static bool nvs_lista;

esp_err_t hfr_config_iniciar(bool nvs_disponible)
{
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        return ESP_ERR_NO_MEM;
    }
    vigente = hfr_config_defecto();
    nvs_lista = nvs_disponible;
    if (nvs_lista) {
        hfr_config_t guardada;
        esp_err_t err = hfr_cargar_config(&guardada);
        if (err == ESP_OK) {
            vigente = guardada;
        } else {
            ESP_LOGW("CONFIG", "Se usan valores iniciales: %s", esp_err_to_name(err));
        }
    }
    return ESP_OK;
}

hfr_config_t hfr_config_actual(void)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    hfr_config_t copia = vigente;
    xSemaphoreGive(mutex);
    return copia;
}

bool hfr_config_aplicar(const cJSON *params, char *motivo, size_t tam)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    hfr_config_t siguiente;
    bool ok = hfr_config_parsear(params, &vigente, &siguiente, motivo, tam);
    if (ok && memcmp(&vigente, &siguiente, sizeof(vigente))) {
        esp_err_t err = nvs_lista ? hfr_guardar_config(&siguiente) : ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            vigente = siguiente;
            ESP_LOGI("CONFIG", "Heladera %lu s [%.2f, %.2f], freezer %lu s [%.2f, %.2f]",
                     (unsigned long)vigente.intervalo_s, vigente.minimo[0] / 100.0, vigente.maximo[0] / 100.0,
                     (unsigned long)vigente.freezer_s, vigente.minimo[1] / 100.0, vigente.maximo[1] / 100.0);
        } else {
            snprintf(motivo, tam, "error_de_nvs");
            ESP_LOGE("CONFIG", "No se aplico el parche: %s", esp_err_to_name(err));
            ok = false;
        }
    }
    xSemaphoreGive(mutex);
    return ok;
}

void hfr_config_describir(cJSON *objeto)
{
    hfr_config_t copia = hfr_config_actual();
    hfr_config_a_json(&copia, objeto);
}
