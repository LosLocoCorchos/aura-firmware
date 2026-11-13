#include "rele.h"
#include "cerradura_pulso.h"
#include "pines.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG = "RELE";

/* Si la logica falla justo en el borde, el corte de hardware llega apenas despues del de ella */
#define MARGEN_CORTE_MS 150

static esp_timer_handle_t temporizador;
static volatile bool energizado = false;

static inline int nivel_reposo(void) { return RELE_ACTIVO_BAJO ? 1 : 0; }
static inline int nivel_activo(void) { return RELE_ACTIVO_BAJO ? 0 : 1; }

void rele_reposo_inicial(void)
{
    /* El nivel se escribe ANTES de pasar el pin a salida: asi sale directo en reposo */
    gpio_set_level(PIN_RELE, nivel_reposo());
    gpio_set_direction(PIN_RELE, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RELE, nivel_reposo());
    energizado = false;
}

static void al_vencer(void *arg)
{
    (void)arg;
    gpio_set_level(PIN_RELE, nivel_reposo());
    energizado = false;
    ESP_EARLY_LOGW(TAG, "Corte por temporizador: cerradura trabada");
}

void rele_iniciar(void)
{
    const esp_timer_create_args_t args = {
        .callback = al_vencer,
        .name = "corte_cerradura",
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &temporizador));
}

void rele_energizar(uint32_t ms)
{
    if (ms > PULSO_TOPE_MS) ms = PULSO_TOPE_MS;       /* tope duro, pase lo que pase arriba */
    if (ms == 0) return;
    esp_timer_stop(temporizador);                      /* ESP_ERR_INVALID_STATE si no corria: no importa */
    /* Primero se arma el corte y despues se energiza: nunca hay un pulso sin temporizador */
    ESP_ERROR_CHECK(esp_timer_start_once(temporizador, (uint64_t)(ms + MARGEN_CORTE_MS) * 1000));
    gpio_set_level(PIN_RELE, nivel_activo());
    energizado = true;
}

void rele_soltar(void)
{
    gpio_set_level(PIN_RELE, nivel_reposo());
    energizado = false;
    esp_timer_stop(temporizador);
}

bool rele_energizado(void)
{
    return energizado;
}
