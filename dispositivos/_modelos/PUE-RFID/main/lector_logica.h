#pragma once
/*
 * Decision de salud del lector RC522 (C puro, se prueba en la compu). lector.c hace lo que
 * esto decide con el driver.
 */
#include <stdbool.h>

typedef enum {
    LECTOR_OK = 0,
    LECTOR_RECUPERADO,      /* estaba apagado (se reinicio) y se lo volvio a levantar */
    LECTOR_CAIDO            /* no responde ni reiniciandolo */
} lector_salud_t;

typedef enum {
    LECTOR_PASO_NADA = 0,
    LECTOR_PASO_ARRANCAR,       /* rc522_start() no se completo nunca: reintentarlo */
    LECTOR_PASO_REINICIAR,      /* arranco, pero los registros dicen que se reinicio */
    LECTOR_PASO_SIN_REMEDIO     /* ni siquiera se pudo crear el driver */
} lector_paso_t;

/* `arrancado`: rc522_start() se completo alguna vez (el estado pasa a POLLING y la tarea del
   driver recien ahi lee tarjetas). Con los registros bien pero sin arrancar el lector NO lee:
   no se lo puede dar por sano. */
static inline lector_paso_t lector_proximo_paso(bool creado, bool arrancado, bool registros_bien)
{
    if (!creado) return LECTOR_PASO_SIN_REMEDIO;
    if (!arrancado) return LECTOR_PASO_ARRANCAR;
    if (!registros_bien) return LECTOR_PASO_REINICIAR;
    return LECTOR_PASO_NADA;
}

static inline lector_salud_t lector_salud_tras(lector_paso_t paso, bool salio_bien)
{
    if (paso == LECTOR_PASO_NADA) return LECTOR_OK;
    if (paso == LECTOR_PASO_SIN_REMEDIO) return LECTOR_CAIDO;
    return salio_bien ? LECTOR_RECUPERADO : LECTOR_CAIDO;
}
