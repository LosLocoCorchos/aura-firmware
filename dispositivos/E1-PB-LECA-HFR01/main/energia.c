#include "energia.h"
#include "almacenamiento.h"
#include "hardware.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nodo_mesh.h"

static bool nvs_lista;
static bool conocida;
static bool bateria;
static bool candidata;
static int64_t cambio_en;

void hfr_energia_iniciar(bool nvs_disponible)
{
    nvs_lista = nvs_disponible;
    conocida = false;
    bateria = false;
    if (HFR_PIN_ALIMENTACION < 0) {
        ESP_LOGW("ENERGIA", "Entrada de alimentacion deshabilitada hasta confirmar el circuito");
        return;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << (HFR_PIN_ALIMENTACION < 0 ? 0 : HFR_PIN_ALIMENTACION),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = HFR_POWER_PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    conocida = nvs_lista && hfr_cargar_alimentacion(&bateria);
    candidata = gpio_get_level(HFR_PIN_ALIMENTACION) != HFR_NIVEL_RED;
    cambio_en = esp_timer_get_time();
}

void hfr_energia_actualizar(bool mesh_iniciada)
{
    if (HFR_PIN_ALIMENTACION < 0) {
        return;
    }
    bool lectura = gpio_get_level(HFR_PIN_ALIMENTACION) != HFR_NIVEL_RED;
    int64_t ahora = esp_timer_get_time();
    if (lectura != candidata) {
        candidata = lectura;
        cambio_en = ahora;
    }
    if (ahora - cambio_en < 200000 || (conocida && lectura == bateria)) {
        return;
    }
    bool notificar = conocida || lectura;
    bateria = lectura;
    conocida = true;
    if (mesh_iniciada && notificar) {
        cJSON *detalles = cJSON_CreateObject();
        if (detalles != NULL) {
            cJSON_AddStringToObject(detalles, "alimentacion", bateria ? "bateria" : "red");
            nodo_mesh_alerta("energia", bateria ? "warning" : "info",
                             bateria ? "Alimentacion por bateria" : "Alimentacion de red recuperada", detalles);
            cJSON_Delete(detalles);
        }
    }
    if (nvs_lista && hfr_guardar_alimentacion(bateria) != ESP_OK) {
        ESP_LOGE("ENERGIA", "No se pudo guardar el estado de alimentacion");
    }
}

bool hfr_energia_bateria(void)
{
    return conocida && bateria;
}
