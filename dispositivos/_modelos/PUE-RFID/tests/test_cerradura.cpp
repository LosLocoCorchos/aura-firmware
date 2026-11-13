#include "../main/cerradura_pulso.h"
#include "../../../../comun/tests/aserciones.h"

int main() {
  cerradura_t c;
  cerradura_iniciar(&c);

  // En reposo no esta energizada ni vence nada
  VERIFICAR(!c.energizada);
  VERIFICAR(!cerradura_vencio(&c, 1000000));

  // Un pulso normal: se concede lo pedido y vence a tiempo
  VERIFICAR(cerradura_pedir(&c, 1000, 5000, 5000) == 5000);
  VERIFICAR(c.energizada);
  VERIFICAR(!cerradura_vencio(&c, 5999));
  VERIFICAR(cerradura_vencio(&c, 6000));
  cerradura_cortar(&c, 6000);
  VERIFICAR(!c.energizada);

  // El tope duro no se pasa ni pidiendo mas ni con set_config mas alto
  cerradura_iniciar(&c);
  VERIFICAR(cerradura_pedir(&c, 0, 60000, 60000) == PULSO_TOPE_MS);
  cerradura_cortar(&c, PULSO_TOPE_MS);
  // El maximo de la puerta (set_config) tambien limita
  cerradura_iniciar(&c);
  VERIFICAR(cerradura_pedir(&c, 0, 8000, 3000) == 3000);

  // Un pulso cortado antes (se abrio la puerta) igual cuenta al menos PULSO_MINIMO_MS
  cerradura_iniciar(&c);
  cerradura_pedir(&c, 0, 5000, 5000);
  cerradura_cortar(&c, 50);
  VERIFICAR(cerradura_usado_ms(&c, 100) == PULSO_MINIMO_MS);   // se contabiliza entero apenas termina
  VERIFICAR(cerradura_usado_ms(&c, 600) == PULSO_MINIMO_MS);

  // Limite por minuto: 20 s de 10 s => dos pulsos enteros y el tercero no entra
  cerradura_iniciar(&c);
  int64_t t = 0;
  VERIFICAR(cerradura_pedir(&c, t, 10000, 10000) == 10000);
  cerradura_cortar(&c, t + 10000);
  t += 10000;
  VERIFICAR(cerradura_pedir(&c, t, 10000, 10000) == 10000);
  cerradura_cortar(&c, t + 10000);
  t += 10000;
  VERIFICAR(cerradura_pedir(&c, t, 10000, 10000) == 0);   // ya hay 20 s en el ultimo minuto
  VERIFICAR(!c.energizada);
  // Al minuto y un segundo solo salio de la ventana 1 s del primer pulso: concede ese segundo
  VERIFICAR(cerradura_pedir(&c, 61000, 10000, 10000) == 1000);
  cerradura_cortar(&c, 62000);
  // Pasado el minuto del primer pulso entero, vuelve a haber lugar para uno completo
  cerradura_iniciar(&c);
  cerradura_pedir(&c, 0, 10000, 10000);
  cerradura_cortar(&c, 10000);
  cerradura_pedir(&c, 10000, 10000, 10000);
  cerradura_cortar(&c, 20000);
  VERIFICAR(cerradura_pedir(&c, 70000, 10000, 10000) == 10000);
  cerradura_cortar(&c, 80000);

  // Con poco margen concede solo lo que queda (si es util) o nada
  cerradura_iniciar(&c);
  cerradura_pedir(&c, 0, 10000, 10000);
  cerradura_cortar(&c, 10000);
  cerradura_pedir(&c, 10000, 9700, 10000);
  cerradura_cortar(&c, 19700);                            // 19,7 s usados, quedan 300 ms: menos del minimo
  VERIFICAR(cerradura_pedir(&c, 19700, 5000, 5000) == 0);

  // Pulsos cortos repetidos tampoco calientan: cada uno cuenta como minimo 500 ms
  cerradura_iniciar(&c);
  int concedidos = 0;
  for (int i = 0; i < 200; i++) {
    int64_t ahora = i * 100;                              // un pedido cada 100 ms
    if (cerradura_pedir(&c, ahora, 5000, 5000) > 0) {
      concedidos++;
      cerradura_cortar(&c, ahora + 10);                   // lo cortan enseguida
    }
  }
  VERIFICAR(concedidos <= LIMITE_MINUTO_MS / PULSO_MINIMO_MS);
  VERIFICAR(cerradura_usado_ms(&c, 20000) <= LIMITE_MINUTO_MS);

  // Pedir con un pulso en curso: el anterior se termina y se cuenta aparte
  cerradura_iniciar(&c);
  cerradura_pedir(&c, 0, 5000, 5000);
  VERIFICAR(cerradura_pedir(&c, 1000, 5000, 5000) == 5000);
  VERIFICAR(c.inicio_ms == 1000);

  // Review: un pedido rechazado por el limite NO toca el pulso en curso (ni lo corta ni lo
  // recorta): el rele sigue energizado hasta su vencimiento y ese tiempo sigue contando
  cerradura_iniciar(&c);
  VERIFICAR(cerradura_pedir(&c, 0, 10000, 10000) == 10000);
  cerradura_cortar(&c, 10000);
  VERIFICAR(cerradura_pedir(&c, 10000, 10000, 10000) == 10000);       // en curso hasta 20000
  VERIFICAR(cerradura_pedir(&c, 19800, 10000, 10000) == 0);           // 19800 ms usados: no entra
  VERIFICAR(c.energizada);
  VERIFICAR(c.inicio_ms == 10000);
  VERIFICAR(c.vence_ms == 20000);
  VERIFICAR(!cerradura_vencio(&c, 19999));
  VERIFICAR(cerradura_vencio(&c, 20000));                             // y vence normalmente
  // El pulso en curso cortado por un pedido aceptado se cuenta con su minimo contable
  cerradura_iniciar(&c);
  cerradura_pedir(&c, 0, 5000, 5000);
  VERIFICAR(cerradura_pedir(&c, 100, 5000, 5000) == 5000);            // corta el de 100 ms (cuenta 500)
  VERIFICAR(cerradura_usado_ms(&c, 100) == PULSO_MINIMO_MS);

  // Tiempos enormes de 64 bits: no hay rollover a los 49,7 dias ni a los 100 dias
  cerradura_iniciar(&c);
  int64_t cien_dias = 100LL * 24 * 3600 * 1000;
  VERIFICAR(cerradura_pedir(&c, cien_dias, 5000, 5000) == 5000);
  VERIFICAR(!cerradura_vencio(&c, cien_dias + 4999));
  VERIFICAR(cerradura_vencio(&c, cien_dias + 5000));

  RESUMEN();
}
