#include "pm2130.h"
#include <math.h>
#include <string.h>

const pm_campo_t PM_CAMPOS[] = {
    /* nombre        registro  tipo         min       max     dec  obligatorio */
    {"v_an_v",       3028,     PM_FLOAT32,  0,        300,    2,   true},
    {"i_a_a",        3000,     PM_FLOAT32,  0,        120,    2,   true},   /* TI 100/5 A */
    {"p_w",          3060,     PM_KW_A_W,   -30000,   30000,  1,   true},
    /* cos φ: factor de DESPLAZAMIENTO (de la fundamental, sin armónicos). No es el factor de
     * potencia verdadero, que también ve los armónicos; ver la ficha. B y C: 3088 y 3090. */
    {"cosphi_a",     3086,     PM_FP_4Q,    0,        1,      2,   true},
#if ENVIAR_FASES_BC
    {"v_bn_v",       3030,     PM_FLOAT32,  0,        300,    2,   false},
    {"v_cn_v",       3032,     PM_FLOAT32,  0,        300,    2,   false},
    {"i_b_a",        3002,     PM_FLOAT32,  0,        120,    2,   false},
    {"i_c_a",        3004,     PM_FLOAT32,  0,        120,    2,   false},
    {"cosphi_b",     3088,     PM_FP_4Q,    0,        1,      2,   false},
    {"cosphi_c",     3090,     PM_FP_4Q,    0,        1,      2,   false},
#endif
#if ENVIAR_THD
    {"thd_v_an_pct", 21330,    PM_FLOAT32,  0,        100,    2,   false},
    {"thd_i_a_pct",  21300,    PM_FLOAT32,  0,        500,    2,   false},
#endif
};
const size_t PM_N_CAMPOS = sizeof(PM_CAMPOS) / sizeof(PM_CAMPOS[0]);

uint16_t pm_crc16(const uint8_t *datos, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= datos[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

void pm_armar_consulta(uint8_t esclavo, uint16_t registro_tabla, uint8_t cantidad, uint8_t out[PM_LARGO_CONSULTA])
{
    uint16_t dir = (uint16_t)(registro_tabla - 1);     /* la trama va en base 0 */
    out[0] = esclavo;
    out[1] = 0x03;
    out[2] = (uint8_t)(dir >> 8);
    out[3] = (uint8_t)(dir & 0xFF);
    out[4] = 0;
    out[5] = cantidad;
    uint16_t c = pm_crc16(out, 6);
    out[6] = (uint8_t)(c & 0xFF);                       /* CRC: byte bajo primero */
    out[7] = (uint8_t)(c >> 8);
}

size_t pm_largo_respuesta(const uint8_t cabecera[3])
{
    if (cabecera[1] & 0x80) return 5;                   /* excepción: esclavo, función, código, CRC */
    return (size_t)3 + cabecera[2] + 2;
}

uint8_t pm_validar_respuesta(uint8_t esclavo, uint8_t cantidad, const uint8_t *rx, size_t n, uint16_t *palabras)
{
    if (n < 5) return PM_ERR_SIN_RESPUESTA;
    if (rx[0] != esclavo) return PM_ERR_TRAMA;
    if (rx[1] == 0x83) {
        if (pm_crc16(rx, 3) != (uint16_t)(rx[3] | (rx[4] << 8))) return PM_ERR_CRC;
        return rx[2] ? rx[2] : PM_ERR_TRAMA;          /* una excepción con código 0 no es una lectura buena */
    }
    if (rx[1] != 0x03 || rx[2] != 2 * cantidad) return PM_ERR_TRAMA;
    size_t largo = (size_t)3 + rx[2];
    if (n < largo + 2) return PM_ERR_SIN_RESPUESTA;
    if (pm_crc16(rx, largo) != (uint16_t)(rx[largo] | (rx[largo + 1] << 8))) return PM_ERR_CRC;
    for (uint8_t i = 0; i < cantidad; i++) {
        palabras[i] = (uint16_t)((rx[3 + 2 * i] << 8) | rx[4 + 2 * i]);
    }
    return PM_OK;
}

bool pm_a_float(const uint16_t w[2], float *v)
{
    if (w[0] == 0xFFC0 && w[1] == 0x0000) return false;
    uint32_t crudo = ((uint32_t)w[0] << 16) | w[1];
    memcpy(v, &crudo, sizeof(*v));
    return !isnan(*v);
}

bool pm_a_int64(const uint16_t w[4], int64_t *v)
{
    if (w[0] == 0x8000 && w[1] == 0 && w[2] == 0 && w[3] == 0) return false;
    uint64_t crudo = 0;
    for (int i = 0; i < 4; i++) crudo = (crudo << 16) | w[i];
    *v = (int64_t)crudo;
    return true;
}

double pm_cosphi_4q(float v)
{
    if (v > 1.0f)       v = 2.0f - v;                   /* cuadrantes en adelanto */
    else if (v < -1.0f) v = -2.0f - v;
    return fabs((double)v);
}

double pm_redondear(double v, int decimales)
{
    double f = pow(10.0, decimales);
    return round(v * f) / f;
}

bool pm_convertir(const pm_campo_t *c, const uint16_t w[2], double *valor)
{
    float f;
    if (!pm_a_float(w, &f)) return false;
    switch (c->tipo) {
    case PM_KW_A_W: *valor = (double)f * 1000.0; break;  /* el registro está en kW */
    case PM_FP_4Q:  *valor = pm_cosphi_4q(f);    break;
    default:        *valor = (double)f;          break;
    }
    *valor = pm_redondear(*valor, c->decimales);
    return true;
}

bool pm_en_rango(const pm_campo_t *c, double valor)
{
    return valor >= c->min && valor <= c->max;
}
