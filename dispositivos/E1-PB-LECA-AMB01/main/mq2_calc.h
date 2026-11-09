/*
 * MQ2: del voltaje en el pin del ADC al voltaje en la salida analógica (AO) del módulo,
 * deshaciendo el divisor resistivo. Sin hardware: se prueba en la PC (tests/).
 *
 *   AO --R1 10k-- GPIO3 --R2 15k-- GND      AO = pin * (R1 + R2) / R2 = pin * 25 / 15
 *
 * Límite del ADC de la ESP32-C3: con 12 dB de atenuación el rango ÚTIL es ~0 a 2500 mV (arriba de
 * eso satura, no son los ~3100 mV de la ESP32 / ESP32-S3). Con este divisor el pin llega a ese
 * tope con el AO en ~4,17 V: un AO más alto (hasta 5 V) satura y NO se puede medir. La alarma
 * (2400 mV de AO = 1440 mV en el pin) está muy por debajo del tope.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MQ2_DIV_R1_OHM   10000u
#define MQ2_DIV_R2_OHM   15000u
/* Techo creíble en el pin de la C3 (a 12 dB): desde acá la lectura está saturada. */
#define MQ2_PIN_MAX_MV   2450u
/* Escala completa nominal del ADC de la C3 a 12 dB: solo para el caso sin calibración de fábrica. */
#define MQ2_FONDO_ESCALA_MV 2500u
/* AO equivalente al techo del pin: si el ADC satura, el AO real es AL MENOS esto. */
#define MQ2_AO_SATURADO_MV  ((MQ2_PIN_MAX_MV * (MQ2_DIV_R1_OHM + MQ2_DIV_R2_OHM)) / MQ2_DIV_R2_OHM)

typedef enum {
    MQ2_LECTURA_OK = 0,
    MQ2_SIN_RESPUESTA,         /* el ADC no devolvió lectura (driver o MQ2 sin iniciar) */
    MQ2_PEGADA_AL_PISO,        /* 0 mV: cable suelto, divisor mal armado o sensor sin alimentar */
    MQ2_SATURADA,              /* el ADC llegó a su techo: el gas es AL MENOS MQ2_AO_SATURADO_MV */
} mq2_estado_t;

/* Convierte mV del pin a mV del AO. Solo si devuelve MQ2_LECTURA_OK, ao_mv es una medición; en
   los otros casos no se toca. */
mq2_estado_t mq2_pin_a_ao_mv(uint32_t pin_mv, uint32_t *ao_mv);
