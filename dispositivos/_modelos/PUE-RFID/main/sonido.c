#include "sonido.h"
#include "sonido_logica.h"
#include "pines.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "SONIDO";

#define BITS_PWM LEDC_TIMER_10_BIT
#define MODO_LEDC LEDC_LOW_SPEED_MODE
#define TIMER_LEDC LEDC_TIMER_0
#define CANAL_LEDC LEDC_CHANNEL_0

static QueueHandle_t cola_puntuales;
static portMUX_TYPE lock_aviso = portMUX_INITIALIZER_UNLOCKED;
static aviso_t aviso_modo = AVISO_NADA;
static int64_t aviso_desde, aviso_hasta;
static uint32_t aviso_generacion;           /* sube con cada cambio: reinicia el conteo de pitidos */

static inline int64_t ahora_ms(void) { return esp_timer_get_time() / 1000; }

static void tono(uint32_t freq, uint32_t volumen)
{
    if (freq == 0 || volumen == 0) {
        ledc_set_duty(MODO_LEDC, CANAL_LEDC, 0);
        ledc_update_duty(MODO_LEDC, CANAL_LEDC);
        return;
    }
    ledc_set_freq(MODO_LEDC, TIMER_LEDC, freq);
    ledc_set_duty(MODO_LEDC, CANAL_LEDC, volumen);
    ledc_update_duty(MODO_LEDC, CANAL_LEDC);
}

static void silencio(void) { tono(0, 0); }

static void tarea_sonido(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    uint32_t generacion_vista = 0, indice = 0;
    int64_t proximo = 0, apagar_en = 0;
    bool sonando = false;

    while (true) {
        esp_task_wdt_reset();
        sonido_t s;
        if (xQueueReceive(cola_puntuales, &s, pdMS_TO_TICKS(10)) == pdTRUE) {
            size_t n;
            const nota_t *notas = sonido_melodia(s, &n);
            for (size_t i = 0; notas != NULL && i < n; i++) {
                tono(notas[i].freq, notas[i].freq ? VOLUMEN_MAX : 0);
                vTaskDelay(pdMS_TO_TICKS(notas[i].ms));
                esp_task_wdt_reset();
            }
            silencio();
            sonando = false;
            proximo = 0;                       /* el aviso, si hay, retoma enseguida */
            continue;
        }

        aviso_t modo;
        int64_t desde, hasta;
        uint32_t generacion;
        taskENTER_CRITICAL(&lock_aviso);
        modo = aviso_modo;
        desde = aviso_desde;
        hasta = aviso_hasta;
        generacion = aviso_generacion;
        taskEXIT_CRITICAL(&lock_aviso);

        int64_t ahora = ahora_ms();
        if (generacion != generacion_vista) {
            generacion_vista = generacion;
            indice = 0;
            proximo = 0;
            silencio();
            sonando = false;
        }
        if (sonando && (ahora - apagar_en) >= 0) {
            silencio();
            sonando = false;
        }
        pitido_t p;
        if (!sonido_aviso(modo, ahora, desde, hasta, indice, &p)) {
            if (sonando) {
                silencio();
                sonando = false;
            }
            continue;
        }
        if ((ahora - proximo) >= 0) {
            tono(p.freq, p.volumen);
            sonando = true;
            apagar_en = ahora + p.dur_ms;
            proximo = ahora + p.espera_ms;
            indice++;
        }
    }
}

void sonido_iniciar(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = MODO_LEDC,
        .duty_resolution = BITS_PWM,
        .timer_num = TIMER_LEDC,
        .freq_hz = 2700,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t canal = {
        .gpio_num = PIN_BUZZER,
        .speed_mode = MODO_LEDC,
        .channel = CANAL_LEDC,
        .timer_sel = TIMER_LEDC,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&canal));
    cola_puntuales = xQueueCreate(8, sizeof(sonido_t));
    xTaskCreate(tarea_sonido, "sonido", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "Sonido listo");
}

void sonido_puntual(sonido_t s)
{
    xQueueSend(cola_puntuales, &s, 0);         /* si esta llena se pierde el pitido, nunca se frena a quien llama */
}

void sonido_aviso_fijar(aviso_t modo, int64_t desde_ms, int64_t hasta_ms)
{
    taskENTER_CRITICAL(&lock_aviso);
    aviso_modo = modo;
    aviso_desde = desde_ms;
    aviso_hasta = hasta_ms;
    aviso_generacion++;
    taskEXIT_CRITICAL(&lock_aviso);
}
