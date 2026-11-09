/*
 * Aviso de ventilación del nodo. No es un detector de gas ni un sistema de seguridad: avisa
 * "abran las ventanas" cuando el aire se carga. Lo decide el nodo con lo que mide, sin depender
 * de la mesh ni de AURA, y se prueba en la PC (sin hardware).
 *
 * Dos causas independientes, cada una con tres estados:
 *   - CO2:   hay mucha gente y no se renueva el aire (SCD40).
 *   - humo:  el ambiente se cargó de humo o vapores, p. ej. soldadura o alcohol isopropílico
 *            (MQ2). El MQ2 no dice qué es ni cuánto: no mide ppm y no está calibrado, es un
 *            indicador grueso.
 *
 *   NORMAL     -> ACTIVA      lecturas válidas sobre el umbral durante ALARMA_FILTRO_MS
 *   ACTIVA     -> SILENCIADA  comando "silence" con la causa activa
 *   SILENCIADA -> ACTIVA      pasaron ALARMA_SILENCIO_MAX_MS y la causa sigue alta
 *   ACTIVA / SILENCIADA -> NORMAL   lecturas válidas bajo el valor "normal" durante ALARMA_FILTRO_MS
 *
 * El minuto en las dos direcciones evita que suene con cada punto de soldadura y que se apague
 * en cuanto baja un instante. Solo una lectura VÁLIDA cambia el estado: si la sonda no da dato,
 * la causa queda como estaba (no prende ni apaga el aviso).
 *
 * Los cambios de estado de cada causa los publica main.c como alertas a AURA
 * (dioxido_carbono_alto, humo_vapores). Los umbrales viven solo acá.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define HUMO_ALARMA_MV   2400u     /* mV en el AO del MQ2: se activa con >=. AJUSTAR tras calibrar en el lab. */
#define HUMO_NORMAL_MV   2200u     /* ...y se normaliza con <: la histéresis evita que titile en el umbral */
#define CO2_ALARMA_PPM   1500u
#define CO2_NORMAL_PPM   1400u

#define ALARMA_FILTRO_MS        60000u      /* 1 min sostenido para prender y para apagar */
#define ALARMA_SILENCIO_MAX_MS  1800000u    /* el silencio vence a los 30 min */
#define ALARMA_HUECO_MAX_MS     30000u      /* sin lecturas válidas por más de esto, se descarta la racha en curso */

#define ALARMA_PITIDO_MS        200u        /* buzzer: pitido corto... */
#define ALARMA_PITIDO_CADA_MS   10000u      /* ...cada tanto, no sirena continua */

typedef enum {
    CAUSA_NORMAL = 0,
    CAUSA_ACTIVA,
    CAUSA_SILENCIADA,
} causa_estado_t;

typedef struct {
    causa_estado_t estado;
    bool racha;                    /* hay una racha de lecturas válidas hacia otro estado */
    bool racha_a_normal;           /* sentido de la racha: true = hacia NORMAL, false = hacia ACTIVA */
    uint64_t racha_desde_ms;
    uint64_t ultima_valida_ms;
    uint64_t silenciada_desde_ms;
    uint32_t valor;                /* última lectura válida (mV o ppm) */
    bool saturado;                 /* esa lectura estaba saturada: el valor real es AL MENOS el informado */
} causa_t;

typedef struct {
    causa_t humo;
    causa_t co2;
    bool activa;                   /* alguna causa fuera de NORMAL (activa o silenciada): el LED la muestra */
} alarma_t;

/* Evalúa una lectura de cada sonda. ahora_ms es el reloj monótono (desde el arranque).
   ok = false significa que esa sonda no dio una lectura válida en este ciclo: no cambia nada.
   humo_saturado: el ADC llegó a su techo; cuenta como lectura válida y alta (humo_mv trae el valor
   mínimo posible). */
void alarma_actualizar(alarma_t *a, uint64_t ahora_ms, bool humo_ok, uint32_t humo_mv, bool humo_saturado,
                       bool co2_ok, uint16_t co2_ppm);

/* Comando "silence": pasa a SILENCIADA las causas que están ACTIVAS ahora. Devuelve false si no
   hay ninguna (no hay nada que callar y no se guarda nada: un silence suelto no calla la próxima
   alarma). */
bool alarma_silenciar(alarma_t *a, uint64_t ahora_ms);

/* true si el buzzer tiene que estar prendido en este instante: pitido corto cada tanto mientras
   alguna causa esté ACTIVA. Una causa SILENCIADA no suena. */
bool alarma_buzzer(const alarma_t *a, uint64_t ahora_ms);
