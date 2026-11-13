#pragma once
#include "logica.h"
void hfr_pantalla_iniciar(void);
void hfr_indicadores(bool alarma, bool conectada);
void hfr_pantalla_actualizar(const float temperaturas[2], uint8_t validas,
                            bool conectada, uint32_t pendientes, bool bateria,
                            uint32_t utc, bool oscura);
