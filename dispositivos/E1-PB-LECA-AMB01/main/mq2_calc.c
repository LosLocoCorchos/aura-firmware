#include "mq2_calc.h"

mq2_estado_t mq2_pin_a_ao_mv(uint32_t pin_mv, uint32_t *ao_mv)
{
    if (pin_mv == 0) {
        return MQ2_PEGADA_AL_PISO;
    }
    if (pin_mv >= MQ2_PIN_MAX_MV) {
        return MQ2_SATURADA;
    }
    uint32_t den = MQ2_DIV_R2_OHM;
    uint32_t num = pin_mv * (MQ2_DIV_R1_OHM + MQ2_DIV_R2_OHM);
    *ao_mv = (num + den / 2) / den;          /* redondeo al mV más cercano */
    return MQ2_LECTURA_OK;
}
