/* Tests de host de main/pm2130.c: trama Modbus, validación de respuestas, conversión y rangos.
 * Los valores de prueba son lecturas reales del PM2130 del LabECA. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "pm2130.h"

static int fallas = 0, total = 0;
#define CHEQUEAR(cond, ...) do { total++; if (!(cond)) { fallas++; printf("FALLA %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void palabras_de_float(float f, uint16_t w[2])
{
    uint32_t c;
    memcpy(&c, &f, sizeof(c));
    w[0] = (uint16_t)(c >> 16);
    w[1] = (uint16_t)(c & 0xFFFF);
}

/* Arma la respuesta que daría el medidor a una lectura de "cant" palabras. */
static size_t respuesta(uint8_t esclavo, const uint16_t *w, uint8_t cant, uint8_t *rx)
{
    rx[0] = esclavo; rx[1] = 0x03; rx[2] = (uint8_t)(2 * cant);
    for (int i = 0; i < cant; i++) { rx[3 + 2 * i] = (uint8_t)(w[i] >> 8); rx[4 + 2 * i] = (uint8_t)(w[i] & 0xFF); }
    size_t n = 3 + 2u * cant;
    uint16_t c = pm_crc16(rx, n);
    rx[n] = (uint8_t)(c & 0xFF); rx[n + 1] = (uint8_t)(c >> 8);
    return n + 2;
}

static const pm_campo_t *campo(const char *nombre)
{
    for (size_t i = 0; i < PM_N_CAMPOS; i++) if (strcmp(PM_CAMPOS[i].nombre, nombre) == 0) return &PM_CAMPOS[i];
    return NULL;
}

static void test_trama(void)
{
    /* Product ID (registro 90): la trama que se verificó contra el medidor real */
    uint8_t tx[PM_LARGO_CONSULTA];
    pm_armar_consulta(1, 90, 1, tx);
    const uint8_t esperada90[] = {0x01, 0x03, 0x00, 0x59, 0x00, 0x01, 0x54, 0x19};
    CHEQUEAR(memcmp(tx, esperada90, 8) == 0, "consulta del registro 90 mal armada");

    /* Tensión A-N: registro 3028 de la tabla = dirección 3027 (0x0BD3) en la trama */
    pm_armar_consulta(1, 3028, 2, tx);
    CHEQUEAR(tx[2] == 0x0B && tx[3] == 0xD3 && tx[5] == 2, "3028 tiene que ir como 0x0BD3 con 2 registros");
    CHEQUEAR(pm_crc16(tx, 6) == (uint16_t)(tx[6] | (tx[7] << 8)), "CRC de la consulta");

    /* Respuesta conocida del Product ID: 15502 = 0x3C8E */
    const uint8_t rx90[] = {0x01, 0x03, 0x02, 0x3C, 0x8E, 0x29, 0x20};
    uint16_t w[4];
    CHEQUEAR(pm_validar_respuesta(1, 1, rx90, sizeof(rx90), w) == PM_OK && w[0] == 15502, "respuesta real del Product ID");
}

static void test_respuestas(void)
{
    uint16_t enviado[2], w[2];
    uint8_t rx[32];
    palabras_de_float(233.58f, enviado);
    size_t n = respuesta(1, enviado, 2, rx);

    CHEQUEAR(pm_largo_respuesta(rx) == n, "largo de una respuesta normal");
    CHEQUEAR(pm_validar_respuesta(1, 2, rx, n, w) == PM_OK && w[0] == enviado[0] && w[1] == enviado[1], "respuesta normal");
    CHEQUEAR(pm_validar_respuesta(1, 2, rx, n - 1, w) == PM_ERR_SIN_RESPUESTA, "respuesta cortada");
    CHEQUEAR(pm_validar_respuesta(2, 2, rx, n, w) == PM_ERR_TRAMA, "respuesta de otro esclavo");
    CHEQUEAR(pm_validar_respuesta(1, 1, rx, n, w) == PM_ERR_TRAMA, "cantidad de bytes distinta a la pedida");
    CHEQUEAR(pm_validar_respuesta(1, 2, rx, 2, w) == PM_ERR_SIN_RESPUESTA, "sin respuesta");
    rx[4] ^= 0x01;
    CHEQUEAR(pm_validar_respuesta(1, 2, rx, n, w) == PM_ERR_CRC, "un bit cambiado tiene que dar error de CRC");

    /* Excepción 02 (dirección ilegal): 5 bytes */
    uint8_t ex[5] = {0x01, 0x83, 0x02, 0, 0};
    uint16_t c = pm_crc16(ex, 3);
    ex[3] = (uint8_t)(c & 0xFF); ex[4] = (uint8_t)(c >> 8);
    CHEQUEAR(pm_largo_respuesta(ex) == 5, "largo de una excepción");
    CHEQUEAR(pm_validar_respuesta(1, 2, ex, 5, w) == 0x02, "excepción 02 del medidor");

    /* Una excepción con código 0 (no existe en Modbus) no puede pasar por una lectura buena */
    uint8_t ex0[5] = {0x01, 0x83, 0x00, 0, 0};
    c = pm_crc16(ex0, 3);
    ex0[3] = (uint8_t)(c & 0xFF); ex0[4] = (uint8_t)(c >> 8);
    CHEQUEAR(pm_validar_respuesta(1, 2, ex0, 5, w) == PM_ERR_TRAMA, "excepción con código 0: error de trama, no PM_OK");
}

static void test_conversion(void)
{
    uint16_t w[2];
    double v;

    palabras_de_float(233.58f, w);
    CHEQUEAR(pm_convertir(campo("v_an_v"), w, &v) && v == 233.58, "tensión: %.10f", v);

    palabras_de_float(0.3285f, w);                       /* el registro de potencia está en kW */
    CHEQUEAR(pm_convertir(campo("p_w"), w, &v) && v == 328.5, "potencia en W: %.10f", v);

    palabras_de_float(0.83f, w);                         /* cos phi en atraso */
    CHEQUEAR(pm_convertir(campo("cosphi_a"), w, &v) && v == 0.83, "cos phi en atraso: %.10f", v);
    palabras_de_float(2.0f - 0.83f, w);                  /* adelanto, codificado 1,17 */
    CHEQUEAR(pm_convertir(campo("cosphi_a"), w, &v) && v == 0.83, "cos phi en adelanto: %.10f", v);
    palabras_de_float(-0.889f, w);                       /* TI invertido (cuadrante III) */
    CHEQUEAR(pm_convertir(campo("cosphi_a"), w, &v) && v == 0.89, "cos phi con potencia recibida: %.10f", v);

    w[0] = 0xFFC0; w[1] = 0x0000;                        /* "no disponible" */
    CHEQUEAR(!pm_convertir(campo("v_an_v"), w, &v), "no disponible no se convierte");
    palabras_de_float(NAN, w);
    CHEQUEAR(!pm_convertir(campo("v_an_v"), w, &v), "NaN no se convierte");

    uint16_t e[4] = {0, 0, 5, 523};                      /* 328 203 Wh, lectura real del banco */
    int64_t wh;
    CHEQUEAR(pm_a_int64(e, &wh) && wh == 328203, "energía INT64");
    uint16_t na[4] = {0x8000, 0, 0, 0};
    CHEQUEAR(!pm_a_int64(na, &wh), "energía no disponible");
}

static void test_rangos_y_campos(void)
{
    CHEQUEAR(pm_en_rango(campo("v_an_v"), 233.58) && !pm_en_rango(campo("v_an_v"), 999), "rango de tensión");
    CHEQUEAR(pm_en_rango(campo("p_w"), -138) && !pm_en_rango(campo("p_w"), 40000), "rango de potencia");
    CHEQUEAR(!pm_en_rango(campo("cosphi_a"), 1.5), "rango de cos phi");

    /* Los obligatorios son exactamente los 4 de la fase A, con los nombres del conector */
    const char *obligatorios[] = {"v_an_v", "i_a_a", "p_w", "cosphi_a"};
    int cuenta = 0;
    for (size_t i = 0; i < PM_N_CAMPOS; i++) if (PM_CAMPOS[i].obligatorio) cuenta++;
    CHEQUEAR(cuenta == 4, "tiene que haber 4 obligatorios, hay %d", cuenta);
    for (int i = 0; i < 4; i++) {
        const pm_campo_t *c = campo(obligatorios[i]);
        CHEQUEAR(c != NULL && c->obligatorio, "falta el obligatorio %s", obligatorios[i]);
    }
    for (size_t i = 0; i < PM_N_CAMPOS; i++) {
        CHEQUEAR(strlen(PM_CAMPOS[i].nombre) <= 24, "nombre muy largo: %s", PM_CAMPOS[i].nombre);
    }
#if ENVIAR_FASES_BC
    CHEQUEAR(campo("v_bn_v") && !campo("v_bn_v")->obligatorio, "fase B presente y no obligatoria");
#else
    CHEQUEAR(campo("v_bn_v") == NULL, "sin ENVIAR_FASES_BC no se mandan las fases B y C");
#endif
}

int main(void)
{
    test_trama();
    test_respuestas();
    test_conversion();
    test_rangos_y_campos();
    printf("%s: %d de %d verificaciones OK (%zu campos)\n", fallas ? "FALLA" : "OK", total - fallas, total, PM_N_CAMPOS);
    return fallas ? 1 : 0;
}
