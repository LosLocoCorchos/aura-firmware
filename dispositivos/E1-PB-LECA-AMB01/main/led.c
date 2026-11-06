#include "led.h"
#include "driver/ledc.h"

/* Pines (ESP32-C3 SuperMini). No son pines de arranque (GPIO2/8/9) ni del puerto serie (20/21). */
#define PIN_LED_R   0
#define PIN_LED_G   1
#define PIN_LED_B   6

/* 0 = cátodo común (el módulo KY-016: el pin "-" va a GND, un nivel alto prende).
   1 = ánodo común (el pin común va a 3V3, un nivel bajo prende). */
#ifndef LED_ANODO_COMUN
#define LED_ANODO_COMUN 0
#endif

#define LED_FRECUENCIA_HZ 5000
#define LED_RESOLUCION    LEDC_TIMER_8_BIT

static const int pines[3] = {PIN_LED_R, PIN_LED_G, PIN_LED_B};

esp_err_t led_iniciar(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LED_RESOLUCION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = LED_FRECUENCIA_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&timer);
    if (e != ESP_OK) {
        return e;
    }
    for (int i = 0; i < 3; i++) {
        ledc_channel_config_t canal = {
            .gpio_num = pines[i],
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t)i,
            .timer_sel = LEDC_TIMER_0,
            .duty = LED_ANODO_COMUN ? 255 : 0,
            .hpoint = 0,
        };
        e = ledc_channel_config(&canal);
        if (e != ESP_OK) {
            return e;
        }
    }
    return ESP_OK;
}

void led_poner(led_rgb_t color)
{
    const uint8_t nivel[3] = {color.r, color.g, color.b};
    for (int i = 0; i < 3; i++) {
        uint32_t duty = LED_ANODO_COMUN ? 255u - nivel[i] : nivel[i];
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)i);
    }
}
