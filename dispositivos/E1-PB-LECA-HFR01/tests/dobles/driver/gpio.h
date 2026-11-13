#pragma once
#include "esp_err.h"
#include <assert.h>
#include <stdint.h>
typedef struct { uint64_t pin_bit_mask; int mode; int pull_up_en; } gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define ESP_ERROR_CHECK(expr) assert((expr) == ESP_OK)
esp_err_t gpio_config(const gpio_config_t *cfg);
int gpio_get_level(int pin);
