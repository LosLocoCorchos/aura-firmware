#include "alarma.h"

bool alarma_actualizar(alarma_t *a, bool gas_ok, uint32_t gas_mv, bool co2_ok, uint16_t co2_ppm)
{
    /* Con histéresis: se activa en el umbral y se apaga recién por debajo del valor "normal". */
    if (!gas_ok) {
        a->gas_activa = false;
    } else if (gas_mv >= GAS_ALARMA_MV) {
        a->gas_activa = true;
    } else if (gas_mv < GAS_NORMAL_MV) {
        a->gas_activa = false;
    }
    if (!co2_ok) {
        a->co2_activa = false;
    } else if (co2_ppm >= CO2_ALARMA_PPM) {
        a->co2_activa = true;
    } else if (co2_ppm < CO2_NORMAL_PPM) {
        a->co2_activa = false;
    }

    /* El silencio de cada causa vale solo mientras esa causa dure */
    if (!a->gas_activa) {
        a->gas_silenciada = false;
    }
    if (!a->co2_activa) {
        a->co2_silenciada = false;
    }
    a->activa = a->gas_activa || a->co2_activa;
    return (a->gas_activa && !a->gas_silenciada) || (a->co2_activa && !a->co2_silenciada);
}

bool alarma_silenciar(alarma_t *a)
{
    if (!a->activa) {
        return false;
    }
    a->gas_silenciada = a->gas_activa;
    a->co2_silenciada = a->co2_activa;
    return true;
}
