#include "../main/config_validacion.h"
#include "../../../../comun/tests/aserciones.h"
#include <cmath>
#include <limits>

int main() {
  int32_t v = -7;
  // Dentro del rango y entero
  VERIFICAR(entero_en_rango(5000, 500, 10000, &v) && v == 5000);
  VERIFICAR(entero_en_rango(500, 500, 10000, &v) && v == 500);        // limites incluidos
  VERIFICAR(entero_en_rango(10000, 500, 10000, &v) && v == 10000);

  // Fuera del rango o no entero: false y la salida no se toca
  v = -7;
  VERIFICAR(!entero_en_rango(499, 500, 10000, &v) && v == -7);
  VERIFICAR(!entero_en_rango(10001, 500, 10000, &v) && v == -7);
  VERIFICAR(!entero_en_rango(5000.5, 500, 10000, &v) && v == -7);
  VERIFICAR(!entero_en_rango(-1, 500, 10000, &v) && v == -7);

  // Review: valores que no entran en int32. Se comparan como double ANTES de convertir
  // (convertir un double fuera del rango de int32 es comportamiento indefinido en C)
  VERIFICAR(!entero_en_rango(1e300, 500, 10000, &v));
  VERIFICAR(!entero_en_rango(-1e300, 500, 10000, &v));
  VERIFICAR(!entero_en_rango(2147483648.0, 500, 10000, &v));
  VERIFICAR(!entero_en_rango(-2147483649.0, -2147483647 - 1, 2147483647, &v));
  VERIFICAR(!entero_en_rango(std::numeric_limits<double>::infinity(), 500, 10000, &v));
  VERIFICAR(!entero_en_rango(-std::numeric_limits<double>::infinity(), 500, 10000, &v));
  VERIFICAR(!entero_en_rango(std::nan(""), 500, 10000, &v));
  VERIFICAR(v == -7);

  // Los extremos de int32 si valen cuando el rango los admite
  VERIFICAR(entero_en_rango(2147483647.0, 0, 2147483647, &v) && v == 2147483647);
  VERIFICAR(entero_en_rango(-2147483648.0, -2147483647 - 1, 0, &v) && v == (-2147483647 - 1));

  RESUMEN();
}
