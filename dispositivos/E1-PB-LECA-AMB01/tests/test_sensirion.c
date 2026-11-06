#include <math.h>
#include <string.h>
#include "sensirion.h"
#include "aserciones.h"

int main(void)
{
    /* Ejemplo de la hoja de datos de Sensirion: CRC de 0xBEEF = 0x92 */
    const uint8_t beef[2] = {0xBE, 0xEF};
    VERIFICAR(sensirion_crc8(beef, 2) == 0x92);

    /* Palabra con CRC bueno y malo */
    const uint8_t buena[3] = {0xBE, 0xEF, 0x92};
    const uint8_t mala[3] = {0xBE, 0xEF, 0x93};
    uint16_t v = 0;
    VERIFICAR(sensirion_palabra(buena, &v) && v == 0xBEEF);
    v = 0;
    VERIFICAR(!sensirion_palabra(mala, &v) && v == 0);

    /* Lectura del SCD4x: 500 ppm, temperatura 0x6667 y humedad 0x5EB9 (hoja de datos del SCD4x) */
    uint8_t rx[9];
    const uint16_t palabras[3] = {0x01F4, 0x6667, 0x5EB9};
    for (int i = 0; i < 3; i++) {
        rx[3 * i] = (uint8_t)(palabras[i] >> 8);
        rx[3 * i + 1] = (uint8_t)(palabras[i] & 0xFF);
        rx[3 * i + 2] = sensirion_crc8(rx + 3 * i, 2);
    }
    uint16_t co2 = 0;
    double t = 0, h = 0;
    VERIFICAR(scd4x_decodificar(rx, &co2, &t, &h));
    VERIFICAR(co2 == 500);
    VERIFICAR(fabs(t - 25.0) < 0.01);            /* -45 + 175 * 0x6667 / 65535 */
    VERIFICAR(fabs(h - 37.0) < 0.05);            /* 100 * 0x5EB9 / 65535 */

    /* CRC de la hoja de datos del SCD4x para temperatura y humedad del ejemplo */
    VERIFICAR(rx[5] == 0xA2 && rx[8] == 0x3C);

    /* Un CRC malo en cualquiera de las tres palabras descarta toda la lectura, sin tocar las salidas */
    for (int i = 0; i < 3; i++) {
        uint8_t mal[9];
        memcpy(mal, rx, sizeof(mal));
        mal[3 * i + 2] ^= 0x01;
        uint16_t c2 = 7;
        double t2 = 7, h2 = 7;
        VERIFICAR(!scd4x_decodificar(mal, &c2, &t2, &h2));
        VERIFICAR(c2 == 7 && t2 == 7 && h2 == 7);
    }

    /* Extremos: todo en cero y todo en 0xFFFF */
    uint8_t cero[9] = {0}, lleno[9];
    for (int i = 0; i < 3; i++) {
        cero[3 * i + 2] = sensirion_crc8(cero + 3 * i, 2);
        lleno[3 * i] = 0xFF;
        lleno[3 * i + 1] = 0xFF;
        lleno[3 * i + 2] = sensirion_crc8(lleno + 3 * i, 2);
    }
    VERIFICAR(scd4x_decodificar(cero, &co2, &t, &h) && co2 == 0 && fabs(t + 45.0) < 1e-9 && h == 0.0);
    VERIFICAR(scd4x_decodificar(lleno, &co2, &t, &h) && co2 == 65535 && fabs(t - 130.0) < 1e-9 && fabs(h - 100.0) < 1e-9);

    printf("ok\n");
    return 0;
}
