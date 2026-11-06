/*
 * MQ2: del voltaje en el pin del ADC al voltaje en la salida analógica (AO) del módulo,
 * deshaciendo el divisor resistivo. Sin hardware: se prueba en la PC (tests/).
 *
 *   AO --R1 10k-- GPIO3 --R2 15k-- GND      AO = pin * (R1 + R2) / R2 = pin * 25 / 15
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MQ2_DIV_R1_OHM   10000u
#define MQ2_DIV_R2_OHM   15000u
/* Con el ADC a 12 dB el pin llega a ~3100 mV: por encima la lectura está saturada. */
#define MQ2_PIN_MAX_MV   3100u

/*
 * false si la lectura no es creíble (pegada al piso o al techo del ADC: cable suelto, divisor
 * mal armado o sensor sin alimentar). Si es creíble, ao_mv queda en mV del AO.
 */
bool mq2_pin_a_ao_mv(uint32_t pin_mv, uint32_t *ao_mv);
