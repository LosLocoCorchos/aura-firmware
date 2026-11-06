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
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600) && !a.activa && !a.silenciada);
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));

    /* silence sin alarma no deja el buzzer callado para la próxima */
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600));
    alarma_silenciar(&a);
    VERIFICAR(!alarma_actualizar(&a, true, 900, true, 600));
    VERIFICAR(alarma_actualizar(&a, true, 3000, true, 600));

    printf("ok\n");
    return 0;
}
