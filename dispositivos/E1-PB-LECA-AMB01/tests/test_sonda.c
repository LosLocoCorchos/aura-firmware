#include "sonda.h"
#include "aserciones.h"

int main(void)
{
    sonda_t s = {0};
    double v = -1;

    /* Sin ninguna lectura no hay dato */
    VERIFICAR(!sonda_vigente(&s, 0, &v));

    /* Una lectura buena queda como última válida, con su instante */
    VERIFICAR(!sonda_registrar(&s, true, 612.0, 1000));
    VERIFICAR(sonda_vigente(&s, 1000, &v) && v == 612.0);

    /* Una lectura mala suelta NO es una falla y se sigue usando el último valor bueno */
    VERIFICAR(!sonda_registrar(&s, false, 0, 6000));
    VERIFICAR(!s.en_falla);
    VERIFICAR(sonda_vigente(&s, 6000, &v) && v == 612.0);          /* observación 6 */
    VERIFICAR(!sonda_registrar(&s, false, 0, 11000));
    VERIFICAR(!s.en_falla && sonda_vigente(&s, 11000, &v) && v == 612.0);

    /* Una buena en el medio reinicia la cuenta de malas seguidas */
    VERIFICAR(!sonda_registrar(&s, true, 640.0, 16000));
    VERIFICAR(!sonda_registrar(&s, false, 0, 21000));
    VERIFICAR(!sonda_registrar(&s, false, 0, 26000));
    VERIFICAR(!s.en_falla && sonda_vigente(&s, 26000, &v) && v == 640.0);

    /* A la 3.ª mala seguida se declara la falla, UNA vez, y ya no hay dato (observación 5) */
    VERIFICAR(sonda_registrar(&s, false, 0, 31000));
    VERIFICAR(s.en_falla && !sonda_vigente(&s, 31000, &v));
    VERIFICAR(!sonda_registrar(&s, false, 0, 36000));               /* sin cambio: no se vuelve a avisar */
    VERIFICAR(!sonda_registrar(&s, false, 0, 41000));
    VERIFICAR(s.en_falla);

    /* La recuperación pide SONDA_OKS_PARA_VOLVER buenas seguidas: una sola no alcanza */
    VERIFICAR(!sonda_registrar(&s, true, 700.0, 46000));
    VERIFICAR(s.en_falla && !sonda_vigente(&s, 46000, &v));
    VERIFICAR(!sonda_registrar(&s, false, 0, 51000));               /* titila: sigue en falla, sin aviso */
    VERIFICAR(!sonda_registrar(&s, true, 710.0, 56000));
    VERIFICAR(sonda_registrar(&s, true, 720.0, 61000));             /* ahora sí: recuperada */
    VERIFICAR(!s.en_falla && sonda_vigente(&s, 61000, &v) && v == 720.0);

    /* Un dato viejo ya no se usa (vigencia) y justo en el límite sí */
    VERIFICAR(sonda_vigente(&s, 61000 + SONDA_VIGENCIA_MS, &v));
    VERIFICAR(!sonda_vigente(&s, 61000 + SONDA_VIGENCIA_MS + 1, &v));
    VERIFICAR(!sonda_vigente(&s, 100, &v));                         /* el reloj no retrocede, pero si pasa: sin dato */

    /* Cuántas alertas "sensor" salen con una sonda que titila: ninguna mientras no haya 3 seguidas */
    sonda_t t = {0};
    int cambios = 0;
    for (int i = 0; i < 200; i++) {
        cambios += sonda_registrar(&t, (i % 3) != 0, 500.0, (uint64_t)i * 5000);   /* 1 mala, 2 buenas */
    }
    VERIFICAR(cambios == 0);

    /* Reintento del sensor: en el ciclo 3 y luego cada 6 */
    VERIFICAR(!sonda_toca_reiniciar(0) && !sonda_toca_reiniciar(1) && !sonda_toca_reiniciar(2));
    VERIFICAR(sonda_toca_reiniciar(3));
    VERIFICAR(!sonda_toca_reiniciar(4) && !sonda_toca_reiniciar(8));
    VERIFICAR(sonda_toca_reiniciar(9) && sonda_toca_reiniciar(15));
    VERIFICAR(!sonda_toca_reiniciar(10));

    printf("ok\n");
    return 0;
}
