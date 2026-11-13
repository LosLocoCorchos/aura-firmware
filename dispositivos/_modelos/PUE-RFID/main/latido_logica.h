#pragma once
/*
 * Intervalo del latido y cuando toca emitirlo (C puro, se prueba en la compu).
 *
 * El intervalo que la puerta informa a AURA (intervalo_s) tiene que ser el que va a usar de
 * verdad: la raiz da por caida a la placa tras 3 intervalos sin datos. Armada, el latido es mas
 * seguido (LATIDO_ARMADA_S) que desarmada (LATIDO_DESARMADA_S).
 */
#include <stdbool.h>
#include <stdint.h>
#include "puerta_logica.h"

typedef struct {
    volatile int intervalo_s;          /* lo lee describir_config desde la tarea de la mesh */
    volatile bool latir_ya;
    int64_t proximo_ms;
} latido_t;

static inline void latido_iniciar(latido_t *l, int intervalo_s)
{
    l->intervalo_s = intervalo_s;
    l->latir_ya = false;
    l->proximo_ms = 0;
}

/* Lo que decidio la logica (armada o no) vale desde ya: el intervalo se actualiza ANTES de que
   salga el reporte, si no el reporte dice el viejo y la raiz da por caida a la placa. */
static inline void latido_sincronizar(latido_t *l, const puerta_t *p)
{
    int nuevo = puerta_latido_s(p);
    if (nuevo != l->intervalo_s) {
        l->intervalo_s = nuevo;
        l->latir_ya = true;
    }
}

/* Ejecutando ACC_REPORTAR: devuelve el intervalo_s que va a salir en el reporte. */
static inline int latido_al_reportar(latido_t *l, const puerta_t *p)
{
    latido_sincronizar(l, p);
    l->latir_ya = true;
    return l->intervalo_s;
}

/* Paso 6 del lazo: true si hay que emitir un latido ahora. */
static inline bool latido_toca(latido_t *l, const puerta_t *p, int64_t ahora_ms)
{
    latido_sincronizar(l, p);
    if (l->latir_ya || (ahora_ms - l->proximo_ms) >= 0) {
        l->latir_ya = false;
        l->proximo_ms = ahora_ms + (int64_t)l->intervalo_s * 1000;
        return true;
    }
    return false;
}
