#include "alarma.h"

bool alarma_actualizar(alarma_t *a, bool gas_ok, uint32_t gas_mv, bool co2_ok, uint16_t co2_ppm)
{
    bool alarma = (gas_ok && gas_mv >= GAS_ALARMA_MV) || (co2_ok && co2_ppm >= CO2_ALARMA_PPM);
    if (!alarma) {
        a->silenciada = false;     /* el silencio vale solo mientras dure esta alarma */
    }
    a->activa = alarma;
    return alarma && !a->silenciada;
}

void alarma_silenciar(alarma_t *a)
{
    a->silenciada = true;
}
