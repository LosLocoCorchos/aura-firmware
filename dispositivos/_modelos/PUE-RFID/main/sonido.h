#pragma once
/*
 * Sonido en su propia tarea: ningun sonido frena la lectura de la puerta ni del lector
 * (en el prototipo los delay() de hasta ~460 ms la frenaban). La tarea esta bajo el watchdog.
 */
#include <stdint.h>
#include "puerta_logica.h"

void sonido_iniciar(void);

/* Una melodia de un solo disparo (se encola; si hay una sonando espera su turno) */
void sonido_puntual(sonido_t s);

/* Aviso continuo (pitidos del pulso, puerta abierta, sirena) con su tope: desde_ms y hasta_ms
   en el reloj de la logica (esp_timer / 1000). AVISO_NADA lo corta. */
void sonido_aviso_fijar(aviso_t modo, int64_t desde_ms, int64_t hasta_ms);
