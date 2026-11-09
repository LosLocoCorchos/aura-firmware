#include "calentamiento.h"
#include "aserciones.h"

int main(void)
{
    /* Son 2 minutos exactos */
    VERIFICAR(CALENTAMIENTO_S == 120u);

    /* Al arrancar está calentando y falta todo el tiempo */
    VERIFICAR(calentamiento_activo(1000, 1000, 120));
    VERIFICAR(calentamiento_restante_s(1000, 1000, 120) == 120);

    /* Justo antes de terminar sigue activo; al cumplirse termina (límite exclusivo) */
    VERIFICAR(calentamiento_activo(0, 119999, 120));
    VERIFICAR(calentamiento_restante_s(0, 119999, 120) == 1);
    VERIFICAR(!calentamiento_activo(0, 120000, 120));
    VERIFICAR(calentamiento_restante_s(0, 120000, 120) == 0);
    VERIFICAR(!calentamiento_activo(0, 500000, 120));
    VERIFICAR(calentamiento_restante_s(0, 500000, 120) == 0);

    /* Los segundos que faltan se redondean hacia arriba (la pantalla nunca marca 0:00 antes de tiempo) */
    VERIFICAR(calentamiento_restante_s(0, 1, 120) == 120);
    VERIFICAR(calentamiento_restante_s(0, 1000, 120) == 119);
    VERIFICAR(calentamiento_restante_s(0, 60000, 120) == 60);
    VERIFICAR(calentamiento_restante_s(0, 60001, 120) == 60);

    /* Si el reloj retrocede se toma como recién empezado, nunca como terminado */
    VERIFICAR(calentamiento_activo(5000, 100, 120));
    VERIFICAR(calentamiento_restante_s(5000, 100, 120) == 120);

    /* El reloj arranca cuando los sensores se inician, no en 0: el tiempo de arranque no cuenta */
    VERIFICAR(calentamiento_activo(8000, 8000 + 119000, 120));
    VERIFICAR(!calentamiento_activo(8000, 8000 + 120000, 120));

    /* Sin duración no hay calentamiento */
    VERIFICAR(!calentamiento_activo(0, 0, 0));

    printf("ok\n");
    return 0;
}
