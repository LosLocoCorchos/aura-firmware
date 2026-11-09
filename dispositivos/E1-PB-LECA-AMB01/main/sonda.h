/*
 * Estado de una sonda: cuándo declararla en falla, cuándo en recuperada y cuál es su última
 * medición válida. Sin hardware: se prueba en la PC.
 *
 *  - Histéresis: una lectura mala suelta no es una falla. Se declara la falla después de
 *    SONDA_FALLAS_PARA_CAER lecturas malas seguidas y la recuperación después de
 *    SONDA_OKS_PARA_VOLVER buenas seguidas. Así AURA recibe una alerta por cambio real y no
 *    una por cada lectura (el SCD40 son 3 sondas: sin esto serían hasta 6 alertas "high" cada 5 s).
 *  - Última válida: se guarda el último valor bueno con su instante. Se usa mientras la sonda no
 *    esté en falla y el dato no sea más viejo que SONDA_VIGENCIA_MS; si no, no hay dato (la sonda
 *    no aparece en "values").
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SONDA_FALLAS_PARA_CAER   3u        /* lecturas malas seguidas para declarar la falla (15 s) */
#define SONDA_OKS_PARA_VOLVER    2u        /* lecturas buenas seguidas para declarar la recuperación */
#define SONDA_VIGENCIA_MS        30000u    /* un valor más viejo que esto ya no se usa */

/* Reinicio del sensor: tras SONDA_REINICIO_DESDE ciclos seguidos sin lectura válida, y después
   cada SONDA_REINICIO_CADA ciclos mientras siga así. */
#define SONDA_REINICIO_DESDE     3u
#define SONDA_REINICIO_CADA      6u

typedef struct {
    bool en_falla;                 /* estado declarado (con histéresis) */
    uint8_t fallas;                /* lecturas malas seguidas (satura en 255) */
    uint8_t oks;                   /* lecturas buenas seguidas (satura en 255) */
    bool tiene_valor;
    double valor;                  /* última medición válida */
    uint64_t t_ms;                 /* instante de esa medición */
} sonda_t;

/* Registra el resultado de una lectura. Devuelve true si cambió el estado declarado (en_falla),
   o sea, si hay que avisarlo. valor solo cuenta si ok. */
bool sonda_registrar(sonda_t *s, bool ok, double valor, uint64_t ahora_ms);

/* Última medición válida, si la sonda no está en falla y el dato todavía es vigente. */
bool sonda_vigente(const sonda_t *s, uint64_t ahora_ms, double *valor);

/* true en el ciclo en que hay que reiniciar el sensor, dado cuántos ciclos seguidos lleva sin
   lectura válida: en el 3.º, y luego cada 6 (3, 9, 15...). */
bool sonda_toca_reiniciar(uint32_t ciclos_sin_lectura);
