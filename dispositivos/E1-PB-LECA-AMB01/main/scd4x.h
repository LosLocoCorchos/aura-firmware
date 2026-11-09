/* SCD40/SCD41 (CO2, temperatura y humedad) por I2C, en modo periódico: mide cada 5 s. */
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* Arranque limpio (wake_up, stop, reinit, número de serie) y start_periodic_measurement.
   ESP_OK = quedó midiendo. Se puede llamar de nuevo para reiniciar el sensor tras una falla:
   el dispositivo se agrega al bus una sola vez. */
esp_err_t scd4x_iniciar(i2c_master_bus_handle_t bus);

/*
 * Lee la medición nueva. Si todavía no hay, espera hasta espera_ms.
 *   ESP_OK                 lectura válida
 *   ESP_ERR_NOT_FOUND      no hubo dato nuevo en ese tiempo
 *   ESP_ERR_INVALID_CRC    la lectura llegó con el CRC mal
 *   otro                   falla del bus I2C
 */
esp_err_t scd4x_leer(uint16_t *co2_ppm, double *temp_c, double *hum_pct, uint32_t espera_ms);
