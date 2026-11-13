#include "../main/latido_logica.h"
#include "../../../../comun/tests/aserciones.h"

static const uint8_t A[4] = {0xAB, 0xE8, 0xB0, 0x22};

// Lo que hace main.c al ejecutar las acciones: cada ACC_REPORTAR manda un reporte con el
// intervalo_s vigente en ese momento. Devuelve el del ultimo reporte (0 si no hubo).
static int reportado(puerta_t &p, latido_t &l) {
  int intervalo = 0;
  accion_t a;
  while (puerta_sacar(&p, &a))
    if (a.tipo == ACC_REPORTAR) intervalo = latido_al_reportar(&l, &p);
  return intervalo;
}

int main() {
  // Armada (latido 30 s) y se desarma con el llavero: el reporte ya dice 120 s, no 30
  {
    puerta_t p;
    puerta_iniciar(&p, 0, true, false, PULSO_DEFECTO_MS, AVISO_DEFECTO_S);
    latido_t l;
    latido_iniciar(&l, puerta_latido_s(&p));
    VERIFICAR(l.intervalo_s == LATIDO_ARMADA_S);
    puerta_tarjeta(&p, 1000, A, 4, true);
    VERIFICAR(reportado(p, l) == LATIDO_DESARMADA_S);
    VERIFICAR(l.intervalo_s == LATIDO_DESARMADA_S);
  }

  // Desarmada (120 s) y se arma con la doble pasada: el reporte ya dice 30 s
  {
    puerta_t p;
    puerta_iniciar(&p, 0, false, false, PULSO_DEFECTO_MS, AVISO_DEFECTO_S);
    latido_t l;
    latido_iniciar(&l, puerta_latido_s(&p));
    puerta_tarjeta(&p, 1000, A, 4, true);
    reportado(p, l);
    puerta_tarjeta(&p, 2500, A, 4, true);
    VERIFICAR(p.armada);
    VERIFICAR(reportado(p, l) == LATIDO_ARMADA_S);
  }

  // El paso 6 del lazo: despues de reportar no repite el cambio ni se atrasa
  {
    puerta_t p;
    puerta_iniciar(&p, 0, true, false, PULSO_DEFECTO_MS, AVISO_DEFECTO_S);
    latido_t l;
    latido_iniciar(&l, puerta_latido_s(&p));
    VERIFICAR(latido_toca(&l, &p, 0));                 // el primero sale enseguida
    VERIFICAR(!latido_toca(&l, &p, 29999));
    VERIFICAR(latido_toca(&l, &p, 30000));             // cada 30 s armada
    puerta_tarjeta(&p, 31000, A, 4, true);             // se desarma
    reportado(p, l);
    VERIFICAR(l.intervalo_s == LATIDO_DESARMADA_S);
    VERIFICAR(latido_toca(&l, &p, 31010));             // el reporte pidio un latido ya
    VERIFICAR(!latido_toca(&l, &p, 31011));
    VERIFICAR(!latido_toca(&l, &p, 31010 + 119999));
    VERIFICAR(latido_toca(&l, &p, 31010 + 120000));    // y de ahi cada 120 s
  }

  RESUMEN();
}
