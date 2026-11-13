#pragma once
/*
 * Rele de la cerradura: lo unico que toca el pin. La cerradura esta TRABADA con el rele suelto
 * (sin corriente) y se destraba solo mientras el rele esta activado (ADR-009, decision 1).
 *
 * El corte de cada pulso lo hace un temporizador de hardware (esp_timer) propio de esta capa,
 * que NO depende de la tarea de la logica: si la logica se cuelga, el temporizador igual suelta
 * el rele, y si es el sistema entero el que se cuelga, el watchdog lo reinicia y el pin vuelve
 * al reposo. Este modulo ademas impone por su cuenta el tope duro de cada pulso.
 */
#include <stdbool.h>
#include <stdint.h>

/* LO PRIMERO que se hace al arrancar (antes de NVS, del lector, de la mesh y de todo lo demas):
   deja el pin en reposo antes de configurarlo como salida, para que no haya un instante con la
   cerradura destrabada. */
void rele_reposo_inicial(void);

/* Crea el temporizador de corte. Despues de rele_reposo_inicial(). */
void rele_iniciar(void);

/* Destraba por `ms` (como mucho el tope duro). El temporizador la vuelve a trabar solo. */
void rele_energizar(uint32_t ms);

/* Traba ya. */
void rele_soltar(void);

bool rele_energizado(void);
