#pragma once
/*
 * Reglas del pulso de la cerradura (C puro, se prueba en la compu). ADR-009, decision 1.
 *
 * La cerradura queda TRABADA en reposo y se destraba solo con un pulso de corriente. Se quema
 * si queda energizada, asi que:
 *   - cada pulso tiene un TOPE de duracion (PULSO_TOPE_MS), que no se puede pasar ni por
 *     set_config ni por una orden;
 *   - hay un LIMITE de tiempo energizada por minuto (LIMITE_MINUTO_MS) en cualquier ventana
 *     movil de 60 s, para que pulsos repetidos no la calienten.
 *
 * Este header solo decide cuanto se puede energizar. Quien corta de verdad es un temporizador
 * de hardware aparte (cerradura.c), respaldado por el watchdog; si la logica se cuelga, el
 * temporizador igual la corta.
 *
 * El tiempo es monotonico en milisegundos de 64 bits (esp_timer_get_time() / 1000): no da la
 * vuelta, y de todos modos se comparan diferencias con signo.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Tope duro de un pulso. 10 s es el valor del ADR cuando la hoja de datos de la Luber 500 no
   dice otra cosa: ajustar cuando se tenga la hoja o la medicion de cuanto aguanta. */
#define PULSO_TOPE_MS 10000
#define PULSO_MINIMO_MS 500            /* menos que esto no vale la pena (ni se contabiliza menos) */
#define LIMITE_MINUTO_MS 20000         /* energizada, como mucho, en cualquier ventana de 60 s */
#define VENTANA_LIMITE_MS 60000
#define HISTORIAL_PULSOS 64            /* 64 x 500 ms = 32 s > LIMITE_MINUTO_MS: nunca se desborda */

typedef struct {
    int64_t ini;
    int64_t fin;
} tramo_t;

typedef struct {
    bool energizada;
    int64_t inicio_ms;
    int64_t vence_ms;
    tramo_t hist[HISTORIAL_PULSOS];    /* pulsos ya terminados, en ronda */
    uint8_t cabeza;
} cerradura_t;

static inline void cerradura_iniciar(cerradura_t *c)
{
    memset(c, 0, sizeof(*c));
}

static inline int64_t tramo_solapado(int64_t ini, int64_t fin, int64_t desde, int64_t hasta)
{
    int64_t a = ini > desde ? ini : desde;
    int64_t b = fin < hasta ? fin : hasta;
    return b > a ? b - a : 0;
}

/* Milisegundos energizada dentro de los ultimos 60 s (cuenta el pulso en curso). */
static inline int64_t cerradura_usado_ms(const cerradura_t *c, int64_t ahora_ms)
{
    int64_t desde = ahora_ms - VENTANA_LIMITE_MS;
    int64_t total = 0;
    for (int i = 0; i < HISTORIAL_PULSOS; i++) {
        /* un pulso terminado se cuenta entero apenas termina (aunque su minimo contable caiga
           un poco en el futuro): asi no se puede pedir otro justo antes de que se contabilice */
        total += tramo_solapado(c->hist[i].ini, c->hist[i].fin, desde, INT64_MAX);
    }
    if (c->energizada) {
        total += tramo_solapado(c->inicio_ms, ahora_ms, desde, ahora_ms);
    }
    return total;
}

static inline void cerradura_cortar(cerradura_t *c, int64_t ahora_ms)
{
    if (!c->energizada) return;
    int64_t fin = ahora_ms;
    if (fin - c->inicio_ms < PULSO_MINIMO_MS) {
        fin = c->inicio_ms + PULSO_MINIMO_MS;     /* un pulso cortito igual se contabiliza entero */
    }
    c->hist[c->cabeza].ini = c->inicio_ms;
    c->hist[c->cabeza].fin = fin;
    c->cabeza = (uint8_t)((c->cabeza + 1) % HISTORIAL_PULSOS);
    c->energizada = false;
}

/* Pide un pulso de pedido_ms (con su tope maximo_ms, que es set_config pero nunca mas que
   PULSO_TOPE_MS). Devuelve cuantos ms se conceden, o 0 si no se puede (se llego al limite por
   minuto). Si ya habia un pulso en curso y el pedido se concede, ese se termina primero y cuenta
   como uno aparte. Si se rechaza (0), NO se toca el pulso en curso: el rele sigue energizado
   hasta su vencimiento y quien lo corta es cerradura_vencio(). */
static inline int64_t cerradura_pedir(cerradura_t *c, int64_t ahora_ms, int64_t pedido_ms, int64_t maximo_ms)
{
    int64_t tope = maximo_ms < PULSO_TOPE_MS ? maximo_ms : PULSO_TOPE_MS;
    int64_t concedido = pedido_ms < tope ? pedido_ms : tope;
    /* Lo usado cuenta el pulso en curso como quedaria si se lo cortara ahora (con su minimo contable) */
    int64_t usado = cerradura_usado_ms(c, ahora_ms);
    if (c->energizada && (ahora_ms - c->inicio_ms) < PULSO_MINIMO_MS) {
        usado += c->inicio_ms + PULSO_MINIMO_MS - ahora_ms;
    }
    int64_t resta = LIMITE_MINUTO_MS - usado;
    if (concedido > resta) concedido = resta;
    if (concedido < PULSO_MINIMO_MS) return 0;
    cerradura_cortar(c, ahora_ms);
    c->energizada = true;
    c->inicio_ms = ahora_ms;
    c->vence_ms = ahora_ms + concedido;
    return concedido;
}

/* true si hay un pulso en curso que ya llego a su tiempo: hay que cortarlo. */
static inline bool cerradura_vencio(const cerradura_t *c, int64_t ahora_ms)
{
    return c->energizada && (ahora_ms - c->vence_ms) >= 0;
}
