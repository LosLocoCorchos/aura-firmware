#include "sondas.h"
#include "hardware.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "esp_log.h"

static onewire_bus_handle_t buses[2];
static ds18b20_device_handle_t sondas[2];

static bool buscar(int s)
{
    if (!buses[s]) {
        return false;
    }
    onewire_device_iter_handle_t iter;
    if (onewire_new_device_iter(buses[s], &iter) != ESP_OK) {
        return false;
    }
    onewire_device_t dispositivo;
    while (onewire_device_iter_get_next(iter, &dispositivo) == ESP_OK) {
        ds18b20_config_t cfg = {};
        if (ds18b20_new_device_from_enumeration(&dispositivo, &cfg, &sondas[s]) == ESP_OK) {
            break;
        }
    }
    onewire_del_device_iter(iter);
    if (!sondas[s]) {
        return false;
    }
    if (ds18b20_set_resolution(sondas[s], DS18B20_RESOLUTION_9B) != ESP_OK) {
        ds18b20_del_device(sondas[s]);
        sondas[s] = NULL;
        return false;
    }
    return true;
}

esp_err_t hfr_sondas_iniciar(void)
{
    const int pines[2] = {HFR_PIN_HELADERA, HFR_PIN_FREEZER};
    esp_err_t resultado = ESP_OK;
    for (int s = 0; s < 2; ++s) {
        onewire_bus_config_t bus = {.bus_gpio_num = pines[s], .flags.en_pull_up = true};
        onewire_bus_rmt_config_t rmt = {.max_rx_bytes = 10};
        esp_err_t err = onewire_new_bus_rmt(&bus, &rmt, &buses[s]);
        if (err != ESP_OK) {
            ESP_LOGE("SONDAS", "Bus %d: %s", s, esp_err_to_name(err));
            resultado = err;
        } else if (!buscar(s)) {
            ESP_LOGW("SONDAS", "Sin DS18B20 en bus %d; se volvera a buscar al medir", s);
        }
    }
    return resultado;
}

bool hfr_sonda_leer(int indice, float *temperatura)
{
    if (indice < 0 || indice > 1 || !buses[indice]) {
        return false;
    }
    if (!sondas[indice] && !buscar(indice)) {
        return false;
    }
    esp_err_t err = ds18b20_trigger_temperature_conversion(sondas[indice]);
    if (err == ESP_OK) {
        err = ds18b20_get_temperature(sondas[indice], temperatura);
    }
    if (err != ESP_OK) {
        ds18b20_del_device(sondas[indice]);
        sondas[indice] = NULL;
        return false;
    }
    return true;
}
