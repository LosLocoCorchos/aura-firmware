#pragma once
/*
 * Validacion numerica de set_config (C puro, se prueba en la compu).
 */
#include <stdbool.h>
#include <stdint.h>

/* true si `v` es un entero dentro de [min, max]; entonces lo deja en *out. Primero compara el
   double contra los limites y solo despues convierte: pasar a int32_t un double fuera de rango
   (o NaN) es comportamiento indefinido en C. Los limites tienen que caber en int32. */
static inline bool entero_en_rango(double v, int32_t min, int32_t max, int32_t *out)
{
    if (!(v >= (double)min && v <= (double)max)) return false;     /* tambien descarta NaN */
    int32_t e = (int32_t)v;
    if ((double)e != v) return false;                              /* 5000.5 no es entero */
    *out = e;
    return true;
}
