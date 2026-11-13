#include "../main/lector_logica.h"
#include "../../../../comun/tests/aserciones.h"

int main() {
  // Todo sano: arranco y los registros estan bien
  VERIFICAR(lector_proximo_paso(true, true, true) == LECTOR_PASO_NADA);
  VERIFICAR(lector_salud_tras(LECTOR_PASO_NADA, true) == LECTOR_OK);

  // Review: cable flojo al encender, rc522_start nunca se completo. Aunque despues haga contacto
  // y los registros den bien, la tarea no lee: hay que reintentar start, no informar "ok"
  VERIFICAR(lector_proximo_paso(true, false, true) == LECTOR_PASO_ARRANCAR);
  VERIFICAR(lector_proximo_paso(true, false, false) == LECTOR_PASO_ARRANCAR);
  VERIFICAR(lector_salud_tras(LECTOR_PASO_ARRANCAR, true) == LECTOR_RECUPERADO);
  VERIFICAR(lector_salud_tras(LECTOR_PASO_ARRANCAR, false) == LECTOR_CAIDO);   // sigue sin lector: sonda en rojo

  // Arranco pero se reinicio (registros mal): se lo reinicia
  VERIFICAR(lector_proximo_paso(true, true, false) == LECTOR_PASO_REINICIAR);
  VERIFICAR(lector_salud_tras(LECTOR_PASO_REINICIAR, true) == LECTOR_RECUPERADO);
  VERIFICAR(lector_salud_tras(LECTOR_PASO_REINICIAR, false) == LECTOR_CAIDO);

  // Nunca se pudo crear el driver: no hay remedio desde aca (sonda en rojo y reinicio de placa)
  VERIFICAR(lector_proximo_paso(false, false, true) == LECTOR_PASO_SIN_REMEDIO);
  VERIFICAR(lector_proximo_paso(false, false, false) == LECTOR_PASO_SIN_REMEDIO);
  VERIFICAR(lector_salud_tras(LECTOR_PASO_SIN_REMEDIO, true) == LECTOR_CAIDO);

  RESUMEN();
}
