#pragma once
#include <stdbool.h>
#include <stdint.h>
void hfr_reloj_iniciar(void);
uint32_t hfr_reloj_utc(void);
void hfr_reloj_mantener(bool mesh_conectada);
