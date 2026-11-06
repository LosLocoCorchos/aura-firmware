#include "sensirion.h"

uint8_t sensirion_crc8(const uint8_t *datos, size_t largo)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < largo; i++) {
        crc ^= datos[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

bool sensirion_palabra(const uint8_t tres[3], uint16_t *valor)
{
    if (sensirion_crc8(tres, 2) != tres[2]) {
        return false;
    }
    *valor = (uint16_t)((tres[0] << 8) | tres[1]);
    return true;
}

bool scd4x_decodificar(const uint8_t rx[9], uint16_t *co2_ppm, double *temp_c, double *hum_pct)
{
    uint16_t co2, t, h;
    if (!sensirion_palabra(rx, &co2) || !sensirion_palabra(rx + 3, &t) || !sensirion_palabra(rx + 6, &h)) {
        return false;
    }
    *co2_ppm = co2;
    *temp_c = -45.0 + 175.0 * (double)t / 65535.0;
    *hum_pct = 100.0 * (double)h / 65535.0;
    return true;
}
