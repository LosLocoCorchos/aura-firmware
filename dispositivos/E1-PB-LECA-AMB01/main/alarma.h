/*
 * Alarma local del nodo: no depende de la mesh ni de AURA. Sin hardware: se prueba en la PC.
 * Los umbrales son los del conector (conectores/E1-PB-LECA-AMB01/conector.py): si se ajustan
 * acá tras calibrar, ajustarlos allá.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define GAS_ALARMA_MV    2400u     /* mV en el AO del MQ2. AJUSTAR tras calibrar. */
#define CO2_ALARMA_PPM   1500u

typedef struct {
    bool activa;                   /* hay alarma ahora */
    bool silenciada;               /* el buzzer está callado hasta que la alarma se normalice */
} alarma_t;

/* Evalúa una medición. Una sonda sin dato válido (ok = false) no cuenta. Devuelve true si el
   buzzer tiene que sonar. */
bool alarma_actualizar(alarma_t *a, bool gas_ok, uint32_t gas_mv, bool co2_ok, uint16_t co2_ppm);

/* Comando "silence": calla el buzzer mientras dure esta alarma. */
void alarma_silenciar(alarma_t *a);
