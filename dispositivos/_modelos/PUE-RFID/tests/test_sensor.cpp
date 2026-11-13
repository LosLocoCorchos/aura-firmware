#include "../main/sensor_logica.h"
#include "../main/puerta_logica.h"
#include "../../../../comun/tests/aserciones.h"

#define BAJO 0
#define ALTO 1

int main() {
  // Antirrebote: un cambio de 30 ms es ruido; uno sostenido 50 ms es la puerta
  {
    sensor_filtro_t f;
    sensor_filtro_iniciar(&f, SENSOR_REED, BAJO, 0);            // reed en BAJO = cerrada
    bool ab = false;
    VERIFICAR(sensor_filtro_paso(&f, 100, SENSOR_REED, ALTO, &ab) == SENSOR_IGUAL);
    VERIFICAR(sensor_filtro_paso(&f, 130, SENSOR_REED, BAJO, &ab) == SENSOR_IGUAL);
    VERIFICAR(sensor_filtro_paso(&f, 200, SENSOR_REED, ALTO, &ab) == SENSOR_IGUAL);
    VERIFICAR(sensor_filtro_paso(&f, 249, SENSOR_REED, ALTO, &ab) == SENSOR_IGUAL);
    VERIFICAR(sensor_filtro_paso(&f, 250, SENSOR_REED, ALTO, &ab) == SENSOR_CAMBIO);
    VERIFICAR(ab);
    VERIFICAR(sensor_filtro_paso(&f, 300, SENSOR_REED, ALTO, &ab) == SENSOR_IGUAL);
  }

  // Review: armada y cerrada con reed (pin BAJO); AURA manda sensor=boton. Para el boton BAJO
  // significa "abierta", pero la puerta no se movio: no hay cambio ni intrusion
  {
    sensor_filtro_t f;
    sensor_filtro_iniciar(&f, SENSOR_REED, BAJO, 0);
    puerta_t p;
    puerta_iniciar(&p, 0, true, false, PULSO_DEFECTO_MS, AVISO_DEFECTO_S);
    bool ab = false;
    int cambios = 0, resincronizadas = 0;
    for (int64_t t = 100; t <= 400; t += 10) {                  // 300 ms con el tipo ya cambiado
      sensor_resultado_t r = sensor_filtro_paso(&f, t, SENSOR_BOTON, BAJO, &ab);
      if (r == SENSOR_CAMBIO) { cambios++; puerta_sensor(&p, t, ab); }
      if (r == SENSOR_RESINCRONIZADO) { resincronizadas++; puerta_sensor_resincronizar(&p, t, ab); }
    }
    VERIFICAR(cambios == 0);
    VERIFICAR(resincronizadas == 1);
    accion_t a;
    bool alerta = false;
    while (puerta_sacar(&p, &a)) alerta |= a.tipo == ACC_ALERTA || a.tipo == ACC_AVISO;
    VERIFICAR(!alerta);
    VERIFICAR(!p.intrusion);
    VERIFICAR(p.abierta == sensor_abierta_de_nivel(SENSOR_BOTON, BAJO));   // ve la lectura nueva
  }

  // Despues de resincronizar, las aperturas de verdad se siguen detectando
  {
    sensor_filtro_t f;
    sensor_filtro_iniciar(&f, SENSOR_REED, BAJO, 0);
    puerta_t p;
    puerta_iniciar(&p, 0, true, false, PULSO_DEFECTO_MS, AVISO_DEFECTO_S);
    bool ab = false;
    // pasa a boton y el pin queda en ALTO ("cerrada" para el boton)
    sensor_resultado_t r = sensor_filtro_paso(&f, 100, SENSOR_BOTON, ALTO, &ab);
    VERIFICAR(r == SENSOR_RESINCRONIZADO);
    VERIFICAR(!ab);
    puerta_sensor_resincronizar(&p, 100, ab);
    VERIFICAR(sensor_filtro_paso(&f, 200, SENSOR_BOTON, BAJO, &ab) == SENSOR_IGUAL);
    VERIFICAR(sensor_filtro_paso(&f, 250, SENSOR_BOTON, BAJO, &ab) == SENSOR_CAMBIO);
    puerta_sensor(&p, 250, ab);
    VERIFICAR(p.intrusion);                                     // armada y se abrio: eso si es intrusion
  }

  // Un cambio de tipo con el sensor ya "abierta" tampoco dispara nada
  {
    sensor_filtro_t f;
    sensor_filtro_iniciar(&f, SENSOR_REED, ALTO, 0);            // reed ALTO = abierta
    bool ab = false;
    VERIFICAR(sensor_filtro_paso(&f, 100, SENSOR_BOTON, ALTO, &ab) == SENSOR_RESINCRONIZADO);
    VERIFICAR(!ab);                                             // para el boton ALTO es cerrada
  }

  RESUMEN();
}
