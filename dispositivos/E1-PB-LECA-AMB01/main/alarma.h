/*
 * Alarma local del nodo: no depende de la mesh ni de AURA. Sin hardware: se prueba en la PC.
 *
 * Cada causa (gas, CO2) tiene su propio estado y su propio silencio. Eso evita que callar el CO2
 * deje mudo el buzzer ante una fuga de gas que aparece después.
 *
 * Los cruces de cada causa (gas_activa / co2_activa) los publica main.c como alertas a AURA
 * (dioxido_carbono_alto, gas_alto): los umbrales viven solo acá, no hay otra copia que sincronizar.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define GAS_ALARMA_MV    2400u     /* mV en el AO del MQ2: la alarma se activa con >=. AJUSTAR tras calibrar. */
#define GAS_NORMAL_MV    2200u     /* ...y se desactiva con <: la histéresis evita que titile en el umbral */
#define CO2_ALARMA_PPM   1500u
#define CO2_NORMAL_PPM   1400u

typedef struct {
    bool gas_activa;               /* el gas está en alarma ahora */
    bool co2_activa;               /* el CO2 está en alarma ahora */
    bool gas_silenciada;           /* el buzzer está callado por el gas hasta que se normalice */
    bool co2_silenciada;           /* ídem por el CO2 */
    bool activa;                   /* hay alguna alarma ahora (gas_activa || co2_activa) */
} alarma_t;

/* Evalúa una medición. Una sonda sin dato válido (ok = false) no cuenta: su causa se apaga.
   Devuelve true si el buzzer tiene que sonar: hay una causa activa que nadie silenció. */
bool alarma_actualizar(alarma_t *a, bool gas_ok, uint32_t gas_mv, bool co2_ok, uint16_t co2_ppm);

/* Comando "silence": calla el buzzer por las causas que están activas AHORA. Cada silencio vale
   hasta que su causa se normalice; una causa nueva vuelve a sonar.
   Devuelve false si no hay ninguna alarma activa (no hay nada que callar y no se guarda nada:
   así un silence suelto no calla la próxima alarma). */
bool alarma_silenciar(alarma_t *a);
