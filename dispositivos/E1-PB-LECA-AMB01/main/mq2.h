/* MQ2 (gas combustible): salida analógica AO -> divisor 10k/15k -> GPIO3 (ADC1). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t mq2_iniciar(void);

/* Promedio de 10 muestras, convertido a mV en el AO. false si la lectura no es creíble. */
bool mq2_leer(uint32_t *ao_mv);
