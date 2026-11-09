#include "led_patron.h"

static bool fase_prendida(uint64_t ahora_ms, uint32_t medio_periodo_ms)
{
    return ((ahora_ms / medio_periodo_ms) % 2u) == 0u;
}

/* Verde / amarillo parpadeando / rojo según la conexión */
static led_rgb_t color_conexion(const led_entrada_t *e, uint64_t ahora_ms)
{
    led_rgb_t apagado = {0, 0, 0};
    if (e->conectada) {
        led_rgb_t v = {0, LED_VERDE_G, 0};
        return v;
    }
    if (e->sin_conexion_ms >= LED_REINTENTO_MS) {
        led_rgb_t r = {LED_ROJO_R, 0, 0};
        return r;
    }
    if (!fase_prendida(ahora_ms, LED_MEDIO_PERIODO_MS)) {
        return apagado;
    }
    led_rgb_t y = {LED_AMARILLO_R, LED_AMARILLO_G, 0};
    return y;
}

/* true en los dos destellos del ciclo: [0,150) y [300,450) ms */
static bool destello_falla(uint64_t ahora_ms)
{
    uint32_t t = (uint32_t)(ahora_ms % LED_FALLA_CICLO_MS);
    return t < LED_FALLA_DESTELLO_MS || (t >= 2u * LED_FALLA_DESTELLO_MS && t < 3u * LED_FALLA_DESTELLO_MS);
}

led_rgb_t led_calcular(const led_entrada_t *e, uint64_t ahora_ms)
{
    led_rgb_t apagado = {0, 0, 0};

    if (e->alarma) {
        if (!fase_prendida(ahora_ms, LED_ALARMA_MEDIO_MS)) {
            return apagado;
        }
        led_rgb_t m = {LED_MAGENTA_R, 0, LED_MAGENTA_B};
        return m;
    }
    if (e->calentando && e->conectada) {        /* sin conexión se ve el intento (amarillo/rojo), no el azul */
        uint32_t t = (uint32_t)(ahora_ms % LED_RESPIRAR_MS);
        uint32_t medio = LED_RESPIRAR_MS / 2u;
        uint32_t tri = t < medio ? t : LED_RESPIRAR_MS - t;      /* 0..medio..0 */
        led_rgb_t a = {0, 0, (uint8_t)(LED_AZUL_MIN + tri * (LED_AZUL_MAX - LED_AZUL_MIN) / medio)};
        return a;
    }
    if (e->sensor_falla && destello_falla(ahora_ms)) {
        led_rgb_t c = {0, LED_CIAN_G, LED_CIAN_B};
        return c;
    }
    return color_conexion(e, ahora_ms);
}
