/* MQ2 (gas combustible): salida analógica AO -> divisor 10k/15k -> GPIO3 (ADC1). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "mq2_calc.h"

esp_err_t mq2_iniciar(void);

/* Promedio de 10 muestras, convertido a mV en el AO. ao_mv solo vale con MQ2_LECTURA_OK; con
   MQ2_SATURADA el gas es al menos MQ2_AO_SATURADO_MV. */
mq2_estado_t mq2_leer(uint32_t *ao_mv);
