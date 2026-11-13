#pragma once
/*
 * Que suena y cuando (C puro, se prueba en la compu). La tarea de sonido (sonido.c) solo
 * traduce esto a PWM; asi el sonido nunca bloquea la lectura de la puerta ni de las tarjetas.
 *
 * Volumen = ciclo de trabajo del PWM de 10 bits (512 = 50% = el maximo para un buzzer pasivo).
 */
#include <stddef.h>
#include <stdint.h>
#include "puerta_logica.h"

#define VOLUMEN_MAX 512
#define VOLUMEN_MIN_PITIDOS 128        /* donde arrancan los pitidos del pulso */
#define VOLUMEN_MIN_AVISO 256          /* donde arranca el aviso de puerta abierta: se tiene que oir desde el principio */
#define RAMPA_AVISO_MS 20000           /* en cuanto llega al maximo el aviso de puerta abierta */

/* ---------- Melodias de un solo disparo ---------- */
typedef struct {
    uint16_t freq;                     /* Hz; 0 = silencio */
    uint16_t ms;
} nota_t;

static const nota_t NOTAS_OK[] = {{988, 80}, {1319, 300}};                       /* moneda */
static const nota_t NOTAS_RECHAZO[] = {{400, 150}, {0, 80}, {400, 150}, {0, 80}};
static const nota_t NOTAS_ERROR[] = {{700, 100}, {0, 60}, {300, 350}};
static const nota_t NOTAS_ARMADO[] = {{1319, 100}, {988, 100}, {659, 250}};      /* bajando */
static const nota_t NOTAS_DESARMADO[] = {{659, 100}, {988, 100}, {1319, 250}};   /* subiendo */

static inline const nota_t *sonido_melodia(sonido_t s, size_t *n)
{
    switch (s) {
    case SON_OK: *n = sizeof(NOTAS_OK) / sizeof(NOTAS_OK[0]); return NOTAS_OK;
    case SON_RECHAZO: *n = sizeof(NOTAS_RECHAZO) / sizeof(NOTAS_RECHAZO[0]); return NOTAS_RECHAZO;
    case SON_ERROR: *n = sizeof(NOTAS_ERROR) / sizeof(NOTAS_ERROR[0]); return NOTAS_ERROR;
    case SON_ARMADO: *n = sizeof(NOTAS_ARMADO) / sizeof(NOTAS_ARMADO[0]); return NOTAS_ARMADO;
    case SON_DESARMADO: *n = sizeof(NOTAS_DESARMADO) / sizeof(NOTAS_DESARMADO[0]); return NOTAS_DESARMADO;
    }
    *n = 0;
    return NULL;
}

/* ---------- Avisos continuos ---------- */
typedef struct {
    uint32_t freq;
    uint32_t volumen;
    uint32_t dur_ms;                   /* cuanto suena el pitido */
    uint32_t espera_ms;                /* desde que empieza este pitido hasta el proximo */
} pitido_t;

/* Pi pi pi que sube de volumen (con el cuadrado del avance: el oido lo percibe parejo) y de
   ritmo a lo largo de rampa_ms, y despues sigue al maximo. */
static inline pitido_t sonido_rampa(int64_t ahora_ms, int64_t desde_ms, int64_t rampa_ms, uint32_t freq,
                                    uint32_t intervalo_inicial, uint32_t intervalo_final, uint32_t vol_min)
{
    int64_t av = rampa_ms > 0 ? (ahora_ms - desde_ms) * 1000 / rampa_ms : 1000;
    if (av < 0) av = 0;
    if (av > 1000) av = 1000;
    pitido_t p;
    p.freq = freq;
    p.volumen = vol_min + (uint32_t)((int64_t)(VOLUMEN_MAX - vol_min) * av * av / 1000000);
    p.dur_ms = 60;
    p.espera_ms = intervalo_inicial - (uint32_t)((int64_t)(intervalo_inicial - intervalo_final) * av / 1000);
    return p;
}

/* El proximo pitido de un aviso continuo. `indice` cuenta los pitidos ya dados (la sirena
   alterna dos tonos). false si el aviso ya termino (paso su tope) o no hay aviso. */
static inline bool sonido_aviso(aviso_t modo, int64_t ahora_ms, int64_t desde_ms, int64_t hasta_ms,
                                uint32_t indice, pitido_t *out)
{
    if (modo == AVISO_NADA || (ahora_ms - hasta_ms) >= 0) return false;
    switch (modo) {
    case AVISO_PITIDOS_PULSO:
        *out = sonido_rampa(ahora_ms, desde_ms, hasta_ms - desde_ms, 2700, 1000, 100, VOLUMEN_MIN_PITIDOS);
        return true;
    case AVISO_PUERTA_ABIERTA:
        *out = sonido_rampa(ahora_ms, desde_ms, RAMPA_AVISO_MS, 2000, 800, 150, VOLUMEN_MIN_AVISO);
        return true;
    case AVISO_INTRUSION:
        out->freq = (indice % 2 == 0) ? 2700 : 1800;
        out->volumen = VOLUMEN_MAX;
        out->dur_ms = 250;
        out->espera_ms = 250;
        return true;
    case AVISO_NADA:
        break;
    }
    return false;
}
