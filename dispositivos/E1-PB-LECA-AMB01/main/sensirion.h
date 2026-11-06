/*
 * Protocolo I2C de los sensores Sensirion (SCD4x): CRC-8 de cada palabra y decodificación
 * de una lectura. Sin hardware: se prueba en la PC (tests/).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* CRC-8 de Sensirion: polinomio 0x31, valor inicial 0xFF. */
uint8_t sensirion_crc8(const uint8_t *datos, size_t largo);

/* Palabra de 16 bits con su CRC (3 bytes: MSB, LSB, CRC). false si el CRC no coincide. */
bool sensirion_palabra(const uint8_t tres[3], uint16_t *valor);

/*
 * Lectura del SCD4x (comando read_measurement): 9 bytes = CO2, temperatura y humedad, cada
 * una con su CRC. false si alguno falla. temp_c y hum_pct salen en double (cJSON los escribe
 * con los decimales que corresponden).
 */
bool scd4x_decodificar(const uint8_t rx[9], uint16_t *co2_ppm, double *temp_c, double *hum_pct);
