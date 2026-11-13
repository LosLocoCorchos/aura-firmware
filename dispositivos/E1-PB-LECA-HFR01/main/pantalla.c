#include "pantalla.h"
#include "bus_i2c.h"
#include "hardware.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static i2c_master_dev_handle_t oled;
static bool encendida;
static uint8_t imagen[8][128];

/* Glifos 5x7 propios, solo para las etiquetas y los numeros de esta pantalla. */
static void glifo(char c, uint8_t columnas[5])
{
    static const uint8_t numeros[10][5] = {
        {62,65,65,65,62}, {0,66,127,64,0}, {98,81,73,73,70}, {34,65,73,73,54},
        {24,20,18,127,16}, {39,69,69,69,57}, {60,74,73,73,48}, {1,113,9,5,3},
        {54,73,73,73,54}, {6,73,73,41,30},
    };
    static const char letras[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const uint8_t mayusculas[26][5] = {
        {126,9,9,9,126}, {127,73,73,73,54}, {62,65,65,65,34}, {127,65,65,34,28},
        {127,73,73,73,65}, {127,9,9,9,1}, {62,65,73,73,58}, {127,8,8,8,127},
        {0,65,127,65,0}, {32,64,65,63,1}, {127,8,20,34,65}, {127,64,64,64,64},
        {127,2,12,2,127}, {127,4,8,16,127}, {62,65,65,65,62}, {127,9,9,9,6},
        {62,65,81,33,94}, {127,9,25,41,70}, {38,73,73,73,50}, {1,1,127,1,1},
        {63,64,64,64,63}, {31,32,64,32,31}, {63,64,56,64,63}, {99,20,8,20,99},
        {3,4,120,4,3}, {97,81,73,69,67},
    };
    memset(columnas, 0, 5);
    if (c >= '0' && c <= '9') {
        memcpy(columnas, numeros[c - '0'], 5);
    } else if (c >= 'A' && c <= 'Z') {
        memcpy(columnas, mayusculas[strchr(letras, c) - letras], 5);
    } else if (c == '-') {
        memset(columnas, 8, 5);
    } else if (c == '.') {
        columnas[2] = 64;
    } else if (c == ':') {
        columnas[2] = 36;
    } else if (c == '/') {
        const uint8_t barra[5] = {32,16,8,4,2};
        memcpy(columnas, barra, 5);
    }
}

static void texto(unsigned pagina, const char *cadena)
{
    for (unsigned x = 0; *cadena != 0 && x + 5 < 128; ++cadena, x += 6) {
        glifo(*cadena, &imagen[pagina][x]);
    }
}

static bool comando(uint8_t valor)
{
    uint8_t datos[2] = {0, valor};
    return i2c_master_transmit(oled, datos, sizeof(datos), 100) == ESP_OK;
}

void hfr_pantalla_iniciar(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << HFR_PIN_LED_UMBRAL) | (1ULL << HFR_PIN_LED_RED),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    hfr_indicadores(false, false);
    if (hfr_i2c_dispositivo(HFR_OLED_DIRECCION, &oled) != ESP_OK) {
        ESP_LOGW("OLED", "SH1106 ausente; se continua sin pantalla");
        return;
    }
    static const uint8_t inicio[] = {
        0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0xad,0x8b,0xa1,0xc8,
        0xda,0x12,0x81,0x7f,0xd9,0x22,0xdb,0x35,0xa4,0xa6,0xaf,
    };
    for (unsigned i = 0; i < sizeof(inicio); ++i) {
        if (!comando(inicio[i])) {
            oled = NULL;
            ESP_LOGW("OLED", "No se pudo inicializar la pantalla");
            return;
        }
    }
    encendida = true;
}

void hfr_indicadores(bool alarma, bool conectada)
{
    gpio_set_level(HFR_PIN_LED_UMBRAL, alarma ? HFR_LED_ACTIVO : !HFR_LED_ACTIVO);
    gpio_set_level(HFR_PIN_LED_RED, conectada ? !HFR_LED_ACTIVO : HFR_LED_ACTIVO);
}

void hfr_pantalla_actualizar(const float temperaturas[2], uint8_t validas,
                            bool conectada, uint32_t pendientes, bool bateria,
                            uint32_t utc, bool oscura)
{
    if (oled == NULL) {
        return;
    }
    if (encendida == oscura) {
        if (!comando(oscura ? 0xae : 0xaf)) {
            return;
        }
        encendida = !oscura;
    }
    if (!encendida) {
        return;
    }
    memset(imagen, 0, sizeof(imagen));
    texto(0, "HELADERA / FREEZER");
    char linea[32];
    for (unsigned i = 0; i < 2; ++i) {
        if ((validas & (1U << i)) != 0) {
            snprintf(linea, sizeof(linea), "%s: %.2f C", i == 0 ? "H" : "F", (double)temperaturas[i]);
        } else {
            snprintf(linea, sizeof(linea), "%s: SIN LECTURA", i == 0 ? "H" : "F");
        }
        texto(2 + i, linea);
    }
    texto(5, conectada ? "MESH CONECTADA" : "SIN MESH");
    snprintf(linea, sizeof(linea), "COLA: %lu", (unsigned long)pendientes);
    texto(6, linea);
    texto(7, bateria ? "BATERIA" : (utc != 0 ? "HORA UTC OK" : "SIN HORA UTC"));
    for (unsigned pagina = 0; pagina < 8; ++pagina) {
        uint8_t datos[129];
        datos[0] = 0x40;
        memcpy(datos + 1, imagen[pagina], 128);
        /* El SH1106 tiene RAM de 132 columnas: el panel visible empieza en la 2. */
        if (!comando(0xb0 + pagina) || !comando(0x02) || !comando(0x10) ||
            i2c_master_transmit(oled, datos, sizeof(datos), 100) != ESP_OK) {
            ESP_LOGW("OLED", "Error al actualizar");
            break;
        }
    }
}
