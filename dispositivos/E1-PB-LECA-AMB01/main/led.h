/* LED RGB de estado por PWM (LEDC). Los colores los decide led_patron.h. */
#pragma once
#include "esp_err.h"
#include "led_patron.h"

/* Configura los tres canales PWM y deja el LED apagado. */
esp_err_t led_iniciar(void);

/* Pone el color. Con LED_ANODO_COMUN el nivel se invierte solo. */
void led_poner(led_rgb_t color);
