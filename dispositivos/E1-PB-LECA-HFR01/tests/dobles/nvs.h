#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef uint32_t nvs_handle_t;
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define ESP_ERR_NVS_NOT_FOUND 0x1102
esp_err_t nvs_open(const char *nombre, int modo, nvs_handle_t *h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *clave, void *datos, size_t *tam);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *clave, const void *datos, size_t tam);
esp_err_t nvs_commit(nvs_handle_t h);
void nvs_close(nvs_handle_t h);
