#include "mq2_calc.h"
#include "aserciones.h"

int main(void)
{
    uint32_t ao = 0;

    /* Divisor 10k/15k: AO = pin * 25 / 15 */
    VERIFICAR(mq2_pin_a_ao_mv(1500, &ao) && ao == 2500);
    VERIFICAR(mq2_pin_a_ao_mv(1440, &ao) && ao == 2400);        /* el umbral de alarma, 2400 mV */
    VERIFICAR(mq2_pin_a_ao_mv(300, &ao) && ao == 500);

    /* Redondea al mV más cercano */
    VERIFICAR(mq2_pin_a_ao_mv(1, &ao) && ao == 2);              /* 1,67 -> 2 */
    VERIFICAR(mq2_pin_a_ao_mv(2, &ao) && ao == 3);              /* 3,33 -> 3 */
    VERIFICAR(mq2_pin_a_ao_mv(4, &ao) && ao == 7);              /* 6,67 -> 7 */

    /* El techo de lo creíble: justo por debajo de 3100 mV en el pin son ~5165 mV en el AO */
    VERIFICAR(mq2_pin_a_ao_mv(3099, &ao) && ao == 5165);

    /* Pegada al piso o al techo del ADC: no es una medición, y la salida no se toca */
    ao = 77;
    VERIFICAR(!mq2_pin_a_ao_mv(0, &ao) && ao == 77);
    VERIFICAR(!mq2_pin_a_ao_mv(3100, &ao) && ao == 77);
    VERIFICAR(!mq2_pin_a_ao_mv(4000, &ao) && ao == 77);

    printf("ok\n");
    return 0;
}
