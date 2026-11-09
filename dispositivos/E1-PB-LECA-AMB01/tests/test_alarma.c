#include <string.h>
#include "alarma.h"
#include "aserciones.h"

int main(void)
{
    alarma_t a = {0};

    /* Sin alarma: todo normal, el buzzer calla */
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600));
    VERIFICAR(!a.activa);

    /* Los umbrales son inclusivos (>=), como en el firmware de Arduino */
    VERIFICAR(!alarma_actualizar(&a, true, GAS_ALARMA_MV - 1, true, CO2_ALARMA_PPM - 1));
    VERIFICAR(alarma_actualizar(&a, true, GAS_ALARMA_MV, true, 600) && a.activa);
    VERIFICAR(alarma_actualizar(&a, true, 900, true, CO2_ALARMA_PPM) && a.activa);

    /* Una sonda sin dato válido no dispara ni tapa a la otra */
    VERIFICAR(!alarma_actualizar(&a, false, 5000, true, 600) && !a.activa);
    VERIFICAR(!alarma_actualizar(&a, true, 900, false, 3000) && !a.activa);
    VERIFICAR(alarma_actualizar(&a, false, 0, true, 2000) && a.activa);

    /* silence: calla el buzzer pero la alarma sigue activa (el display la muestra) */
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));
    alarma_silenciar(&a);
    VERIFICAR(!alarma_actualizar(&a, true, 3000, true, 600) && a.activa);
    VERIFICAR(!alarma_actualizar(&a, true, 2500, true, 600) && a.activa);

    /* El silencio vale solo mientras dure esta alarma: al normalizarse se rearma */
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600) && !a.activa && !a.gas_silenciada && !a.co2_silenciada);
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));

    /* silence sin alarma no deja el buzzer callado para la próxima */
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600));
    alarma_silenciar(&a);
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600));
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));

    /* --- Observación 1 del profesor: el silencio es por causa, no global ---------------- */
    memset(&a, 0, sizeof a);
    VERIFICAR(alarma_actualizar(&a, true, 900, true, 1600));      /* CO2 alto, suena */
    alarma_silenciar(&a);                                         /* lo callan */
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 1600));
    /* Aparece una fuga de gas con el CO2 todavía alto: es una causa nueva y tiene que sonar */
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 1600));
    /* Callar de nuevo calla las dos; el CO2 se normaliza y el gas sigue callado */
    alarma_silenciar(&a);
    VERIFICAR(!alarma_actualizar(&a, true, 3000, true, 600));
    /* El CO2 vuelve a subir mientras el gas sigue en alarma: causa nueva, suena */
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 1700));

    /* --- Observación 2: un silence sin alarma activa no se guarda ------------------------ */
    memset(&a, 0, sizeof a);
    VERIFICAR(!alarma_silenciar(&a));                             /* p. ej. en el calentamiento: se rechaza */
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));      /* la primera alarma suena */
    VERIFICAR(alarma_silenciar(&a));                              /* con alarma activa sí se acepta */
    memset(&a, 0, sizeof a);
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600));
    VERIFICAR(!alarma_silenciar(&a));                             /* 1 s antes de que arranque */
    VERIFICAR(alarma_actualizar(&a, false, 0, true, 2000));

    /* silence cuando la alarma ya pasó: tampoco se guarda */
    memset(&a, 0, sizeof a);
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600));
    VERIFICAR(!alarma_silenciar(&a));
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));

    /* --- Histéresis: no titila alrededor del umbral ------------------------------------- */
    memset(&a, 0, sizeof a);
    VERIFICAR(alarma_actualizar(&a, true, 900, true, CO2_ALARMA_PPM));
    VERIFICAR(alarma_actualizar(&a, true, 900, true, CO2_ALARMA_PPM - 1) && a.co2_activa);
    VERIFICAR(alarma_actualizar(&a, true, 900, true, CO2_NORMAL_PPM) && a.co2_activa);
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, CO2_NORMAL_PPM - 1) && !a.co2_activa);
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, CO2_ALARMA_PPM - 1));   /* de abajo no se activa */
    VERIFICAR(alarma_actualizar(&a, true, GAS_ALARMA_MV, true, 600));
    VERIFICAR(alarma_actualizar(&a, true, GAS_NORMAL_MV, true, 600) && a.gas_activa);
    VERIFICAR(!alarma_actualizar(&a, true, GAS_NORMAL_MV - 1, true, 600) && !a.gas_activa);

    /* Una sonda sin dato apaga su causa (no queda alarma colgada de un valor viejo) */
    memset(&a, 0, sizeof a);
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));
    VERIFICAR(!alarma_actualizar(&a, false, 0, true, 600) && !a.gas_activa && !a.activa);

    printf("ok\n");
    return 0;
}
