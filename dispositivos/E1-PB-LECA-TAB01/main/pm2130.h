/*
 * pm2130: lo propio del medidor Schneider EasyLogic PM2130, sin nada de ESP-IDF, para poder
 * probarlo en la PC (tests/). Arma la consulta Modbus RTU, valida la respuesta, convierte los
 * registros y sabe qué campos se mandan y con qué rango.
 *
 * Registros: número de la TABLA de Schneider (base 1). En la trama va registro - 1.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Qué se envía, además de los obligatorios de la fase A (1 = se envía) ----
 * Si se cambia, revisar conectores/E1-PB-LECA-TAB01/conector.toml. */
#ifndef ENVIAR_FASES_BC
#define ENVIAR_FASES_BC   0     /* el LabECA es monofásico */
#endif
#ifndef ENVIAR_THD
#define ENVIAR_THD        0
#endif
#ifndef ENVIAR_TANFI
#define ENVIAR_TANFI      0
#endif
#ifndef ENVIAR_ENERGIA
#define ENVIAR_ENERGIA    0
#endif

/* Resultado de una lectura: 0 = bien; 0x01..0x0B = excepción Modbus del medidor. */
#define PM_OK                 0x00
#define PM_ERR_TRAMA          0xE0    /* esclavo, función o largo inesperados */
#define PM_ERR_SIN_RESPUESTA  0xE2
#define PM_ERR_CRC            0xE3

#define PM_LARGO_CONSULTA     8

uint16_t pm_crc16(const uint8_t *datos, size_t n);

/* Arma la consulta FC03 (leer holding registers) en out[8]. */
void pm_armar_consulta(uint8_t esclavo, uint16_t registro_tabla, uint8_t cantidad, uint8_t out[PM_LARGO_CONSULTA]);

/* Largo que tiene que tener la respuesta, según los 3 primeros bytes recibidos
   (excepción: 5 bytes; normal: 5 + 2 * cantidad). */
size_t pm_largo_respuesta(const uint8_t cabecera[3]);

/* Valida una respuesta completa y, si está bien, copia las palabras. */
uint8_t pm_validar_respuesta(uint8_t esclavo, uint8_t cantidad, const uint8_t *rx, size_t n, uint16_t *palabras);

/* FLOAT32 de Schneider (palabra alta primero). false si es "no disponible" (0xFFC00000) o NaN. */
bool pm_a_float(const uint16_t w[2], float *v);

/* INT64 de Schneider (4 palabras, la más significativa primero). false si es "no disponible". */
bool pm_a_int64(const uint16_t w[4], int64_t *v);

/* Factor de potencia de 4 cuadrantes (-2..2) -> módulo del cos phi (0..1). */
double pm_cosphi_4q(float v);

/* Redondea para que cJSON escriba 233.58 y no 233.580001831. */
double pm_redondear(double v, int decimales);

/* ---- Campos de 2 registros (FLOAT32) ---- */
typedef enum { PM_FLOAT32, PM_KW_A_W, PM_FP_4Q } pm_tipo_t;

typedef struct {
    const char *nombre;      /* el nombre en values, con la unidad */
    uint16_t    registro;    /* tabla de Schneider */
    pm_tipo_t   tipo;
    double      min, max;    /* rango físico (el mismo que conector.toml) */
    int         decimales;
    bool        obligatorio; /* true: tiene sonda y alerta si falla */
} pm_campo_t;

extern const pm_campo_t PM_CAMPOS[];
extern const size_t PM_N_CAMPOS;

/* Convierte las 2 palabras de un campo al valor que va en values. false = no disponible. */
bool pm_convertir(const pm_campo_t *c, const uint16_t w[2], double *valor);
bool pm_en_rango(const pm_campo_t *c, double valor);

/* ---- Opcionales que no son un FLOAT32 directo ---- */
#define PM_REG_Q_TOTAL      3068      /* kVAR, para tan phi = Q / P */
#define PM_REG_ENERGIA      3204      /* Wh, INT64 */
#define PM_P_MIN_TANFI_W    10.0      /* con menos carga, tan phi no es confiable */
#define PM_TANFI_MIN        (-10.0)
#define PM_TANFI_MAX        10.0
#define PM_ENERGIA_MAX_KWH  1e9
