/*
 * Calentamiento de los sensores. Sin hardware: se prueba en la PC.
 *
 * Los dos sensores (SCD40 y MQ2) arrancan juntos y comparten UN solo reloj, que se pone en marcha
 * cuando ambos ya están iniciados. Mientras corre el calentamiento no se publica nada a AURA, no
 * se informa el estado de las sondas y la alarma/buzzer quedan inhibidos. El reloj es el tiempo
 * desde el arranque del chip: tras cualquier reinicio (por ejemplo un corte de luz) el
 * calentamiento vuelve a empezar completo.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CALENTAMIENTO_S   120u     /* 2 minutos */

/* true mientras no pasaron duracion_s segundos desde desde_ms. Si el reloj retrocede
   (ahora_ms < desde_ms) se considera que recién empieza. */
bool calentamiento_activo(uint64_t desde_ms, uint64_t ahora_ms, uint32_t duracion_s);

/* Segundos que faltan, redondeando hacia arriba (0 cuando terminó). */
uint32_t calentamiento_restante_s(uint64_t desde_ms, uint64_t ahora_ms, uint32_t duracion_s);
