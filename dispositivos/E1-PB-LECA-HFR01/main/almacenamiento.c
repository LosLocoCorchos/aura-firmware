#include "almacenamiento.h"
#include "nvs.h"

typedef struct {
    uint32_t magic;
    hfr_config_t config;
    uint32_t crc;
} config_guardada_t;

static esp_err_t guardar(const char *clave, const void *datos, size_t tam)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("hfr_idf", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, clave, datos, tam);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t cargar(const char *clave, void *datos, size_t tam)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("hfr_idf", NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    size_t longitud = tam;
    err = nvs_get_blob(h, clave, datos, &longitud);
    nvs_close(h);
    return err == ESP_OK && longitud != tam ? ESP_ERR_INVALID_SIZE : err;
}

esp_err_t hfr_guardar_config(const hfr_config_t *cfg)
{
    if (!hfr_config_valida(cfg)) {
        return ESP_ERR_INVALID_ARG;
    }
    config_guardada_t dato = {.magic = 0x48464931, .config = *cfg};
    dato.crc = hfr_crc32(&dato, offsetof(config_guardada_t, crc));
    return guardar("config", &dato, sizeof(dato));
}

esp_err_t hfr_cargar_config(hfr_config_t *cfg)
{
    config_guardada_t dato = {0};
    esp_err_t err = cargar("config", &dato, sizeof(dato));
    if (err != ESP_OK) {
        return err;
    }
    if (dato.magic != 0x48464931 || !hfr_config_valida(&dato.config) ||
        dato.crc != hfr_crc32(&dato, offsetof(config_guardada_t, crc))) {
        return ESP_ERR_INVALID_CRC;
    }
    *cfg = dato.config;
    return ESP_OK;
}

esp_err_t hfr_guardar_alimentacion(bool bateria)
{
    uint8_t valor = bateria;
    return guardar("bateria", &valor, sizeof(valor));
}

bool hfr_cargar_alimentacion(bool *bateria)
{
    uint8_t valor;
    if (cargar("bateria", &valor, sizeof(valor)) != ESP_OK || valor > 1) {
        return false;
    }
    *bateria = valor;
    return true;
}
