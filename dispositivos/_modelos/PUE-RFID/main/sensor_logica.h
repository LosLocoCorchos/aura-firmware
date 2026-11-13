#pragma once
/*
 * Lectura del sensor de la puerta con antirrebote (C puro, se prueba en la compu).
 * Pasa el nivel crudo del pin a "abierta" segun el tipo de sensor y avisa solo cuando el cambio
 * se mantuvo ANTIREBOTE_SENSOR_MS.
 */
#include <stdbool.h>
#include <stdint.h>

#define ANTIREBOTE_SENSOR_MS 50

typedef enum {
    SENSOR_REED = 0,        /* abierta = pin en ALTO (iman lejos o cable cortado: un corte se lee "abierta") */
    SENSOR_BOTON            /* abierta = pin en BAJO (boton de banco que simula la puerta) */
} sensor_tipo_t;

typedef enum {
    SENSOR_IGUAL = 0,       /* nada que informar */
    SENSOR_CAMBIO,          /* la puerta se abrio o se cerro (*abierta) */
    SENSOR_RESINCRONIZADO   /* cambio el tipo de sensor: *abierta es la lectura de ahora, sin evento */
} sensor_resultado_t;

typedef struct {
    sensor_tipo_t tipo;                /* con el que se interpreta el pin */
    bool estable;
    bool ultima;
    int64_t cambio_en_ms;
} sensor_filtro_t;

static inline bool sensor_abierta_de_nivel(sensor_tipo_t tipo, int nivel)
{
    return tipo == SENSOR_REED ? nivel == 1 : nivel == 0;
}

static inline void sensor_filtro_iniciar(sensor_filtro_t *f, sensor_tipo_t tipo, int nivel, int64_t ahora_ms)
{
    f->tipo = tipo;
    f->estable = f->ultima = sensor_abierta_de_nivel(tipo, nivel);
    f->cambio_en_ms = ahora_ms;
}

static inline sensor_resultado_t sensor_filtro_paso(sensor_filtro_t *f, int64_t ahora_ms, sensor_tipo_t tipo,
                                                    int nivel, bool *abierta)
{
    bool cruda = sensor_abierta_de_nivel(tipo, nivel);
    if (tipo != f->tipo) {
        /* Cambiar reed <-> boton invierte como se lee el pin sin que la puerta se mueva: la
           lectura de ahora pasa a ser el estado estable, no un cambio */
        f->tipo = tipo;
        f->estable = f->ultima = cruda;
        f->cambio_en_ms = ahora_ms;
        *abierta = cruda;
        return SENSOR_RESINCRONIZADO;
    }
    if (cruda != f->ultima) {
        f->ultima = cruda;
        f->cambio_en_ms = ahora_ms;
    }
    if (cruda != f->estable && (ahora_ms - f->cambio_en_ms) >= ANTIREBOTE_SENSOR_MS) {
        f->estable = cruda;
        *abierta = cruda;
        return SENSOR_CAMBIO;
    }
    return SENSOR_IGUAL;
}
