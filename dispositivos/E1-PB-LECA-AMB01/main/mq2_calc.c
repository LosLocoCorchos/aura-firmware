#include "mq2_calc.h"

bool mq2_pin_a_ao_mv(uint32_t pin_mv, uint32_t *ao_mv)
{
    if (pin_mv == 0 || pin_mv >= MQ2_PIN_MAX_MV) {
        return false;
    }
    uint32_t den = MQ2_DIV_R2_OHM;
    uint32_t num = pin_mv * (MQ2_DIV_R1_OHM + MQ2_DIV_R2_OHM);
    *ao_mv = (num + den / 2) / den;          /* redondeo al mV más cercano */
    return true;
}
