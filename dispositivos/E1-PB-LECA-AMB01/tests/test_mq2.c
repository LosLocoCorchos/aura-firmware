#include "mq2_calc.h"
#include "aserciones.h"

int main(void)
{
    uint32_t ao = 0;

    /* Divisor 10k/15k: AO = pin * 25 / 15 */
    VERIFICAR(mq2_pin_a_ao_mv(1500, &ao) == MQ2_LECTURA_OK && ao == 2500);
    VERIFICAR(mq2_pin_a_ao_mv(1440, &ao) == MQ2_LECTURA_OK && ao == 2400);   /* el umbral de alarma, 2400 mV */
    VERIFICAR(mq2_pin_a_ao_mv(300, &ao) == MQ2_LECTURA_OK && ao == 500);

    /* Redondea al mV más cercano */
    VERIFICAR(mq2_pin_a_ao_mv(1, &ao) == MQ2_LECTURA_OK && ao == 2);          /* 1,67 -> 2 */
    VERIFICAR(mq2_pin_a_ao_mv(2, &ao) == MQ2_LECTURA_OK && ao == 3);          /* 3,33 -> 3 */
    VERIFICAR(mq2_pin_a_ao_mv(4, &ao) == MQ2_LECTURA_OK && ao == 7);          /* 6,67 -> 7 */

    /* Observación 7: el ADC de la C3 a 12 dB satura cerca de 2500 mV, no de 3100 mV */
    VERIFICAR(MQ2_PIN_MAX_MV <= 2500u);
    VERIFICAR(mq2_pin_a_ao_mv(MQ2_PIN_MAX_MV - 1, &ao) == MQ2_LECTURA_OK && ao == 4082);
    ao = 77;
    VERIFICAR(mq2_pin_a_ao_mv(MQ2_PIN_MAX_MV, &ao) == MQ2_SATURADA && ao == 77);
    /* Lo que antes (límite de 3100 mV) se daba por válido, ~4200 mV de AO, ahora es saturación */
    VERIFICAR(mq2_pin_a_ao_mv(2520, &ao) == MQ2_SATURADA && ao == 77);
    VERIFICAR(mq2_pin_a_ao_mv(3099, &ao) == MQ2_SATURADA && ao == 77);
    VERIFICAR(mq2_pin_a_ao_mv(4000, &ao) == MQ2_SATURADA && ao == 77);
    VERIFICAR(MQ2_AO_SATURADO_MV == 4083u);
    /* Saturada siempre es "gas alto" para la alarma: el techo equivalente supera el umbral */
    VERIFICAR(MQ2_AO_SATURADO_MV > 2400u);

    /* Pegada al piso: no es una medición, y la salida no se toca */
    VERIFICAR(mq2_pin_a_ao_mv(0, &ao) == MQ2_PEGADA_AL_PISO && ao == 77);

    printf("ok\n");
    return 0;
}
