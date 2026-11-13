#include "../main/sonido_logica.h"
#include "../../../../comun/tests/aserciones.h"

int main() {
  // Cada sonido de un disparo tiene su melodia
  const sonido_t todos[] = {SON_OK, SON_RECHAZO, SON_ERROR, SON_ARMADO, SON_DESARMADO};
  for (sonido_t s : todos) {
    size_t n = 0;
    const nota_t *notas = sonido_melodia(s, &n);
    VERIFICAR(notas != NULL && n > 0);
  }

  pitido_t p;

  // Pitidos del pulso: arrancan bajos y lentos, terminan fuertes y rapidos
  VERIFICAR(sonido_aviso(AVISO_PITIDOS_PULSO, 0, 0, 10000, 0, &p));
  VERIFICAR(p.volumen == VOLUMEN_MIN_PITIDOS && p.espera_ms == 1000 && p.freq == 2700);
  VERIFICAR(sonido_aviso(AVISO_PITIDOS_PULSO, 9999, 0, 10000, 0, &p));
  VERIFICAR(p.volumen > 480 && p.volumen <= VOLUMEN_MAX && p.espera_ms < 120);
  uint32_t anterior = 0;
  for (int64_t t = 0; t < 10000; t += 500) {            // el volumen nunca baja
    VERIFICAR(sonido_aviso(AVISO_PITIDOS_PULSO, t, 0, 10000, 0, &p));
    VERIFICAR(p.volumen >= anterior);
    anterior = p.volumen;
  }
  // Pasado el tope no suena
  VERIFICAR(!sonido_aviso(AVISO_PITIDOS_PULSO, 10000, 0, 10000, 0, &p));

  // Aviso de puerta abierta: se oye desde el principio y llega al maximo a los 20 s
  VERIFICAR(sonido_aviso(AVISO_PUERTA_ABIERTA, 1000, 1000, 121000, 0, &p));
  VERIFICAR(p.volumen == VOLUMEN_MIN_AVISO);
  VERIFICAR(sonido_aviso(AVISO_PUERTA_ABIERTA, 1000 + RAMPA_AVISO_MS, 1000, 121000, 0, &p));
  VERIFICAR(p.volumen == VOLUMEN_MAX && p.espera_ms == 150);
  VERIFICAR(sonido_aviso(AVISO_PUERTA_ABIERTA, 100000, 1000, 121000, 0, &p));
  VERIFICAR(p.volumen == VOLUMEN_MAX);
  // Tope de 2 min: despues de `hasta` calla
  VERIFICAR(!sonido_aviso(AVISO_PUERTA_ABIERTA, 121000, 1000, 121000, 0, &p));

  // Sirena de intrusion: alterna dos tonos al maximo
  VERIFICAR(sonido_aviso(AVISO_INTRUSION, 0, 0, 120000, 0, &p) && p.freq == 2700 && p.volumen == VOLUMEN_MAX);
  VERIFICAR(sonido_aviso(AVISO_INTRUSION, 0, 0, 120000, 1, &p) && p.freq == 1800);
  VERIFICAR(!sonido_aviso(AVISO_INTRUSION, 120000, 0, 120000, 0, &p));

  // Sin aviso no suena nada
  VERIFICAR(!sonido_aviso(AVISO_NADA, 0, 0, 100, 0, &p));

  // Tiempos de 64 bits grandes (100 dias)
  int64_t t0 = 100LL * 24 * 3600 * 1000;
  VERIFICAR(sonido_aviso(AVISO_INTRUSION, t0 + 1000, t0, t0 + 120000, 0, &p));
  VERIFICAR(!sonido_aviso(AVISO_INTRUSION, t0 + 120000, t0, t0 + 120000, 0, &p));

  RESUMEN();
}
