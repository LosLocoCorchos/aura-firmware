#include "led_patron.h"
#include "aserciones.h"

static int igual(led_rgb_t c, uint8_t r, uint8_t g, uint8_t b)
{
    return c.r == r && c.g == g && c.b == b;
}

int main(void)
{
    led_entrada_t e = {0};

    /* Conectada: verde fijo, a cualquier hora */
    e.conectada = true;
    for (uint64_t t = 0; t < 5000; t += 137) {
        VERIFICAR(igual(led_calcular(&e, t), 0, LED_VERDE_G, 0));
    }

    /* Recién arrancada o sin conexión hace poco: amarillo parpadeando 500/500 ms */
    e.conectada = false;
    e.sin_conexion_ms = 0;
    VERIFICAR(igual(led_calcular(&e, 0), LED_AMARILLO_R, LED_AMARILLO_G, 0));
    VERIFICAR(igual(led_calcular(&e, 499), LED_AMARILLO_R, LED_AMARILLO_G, 0));
    VERIFICAR(igual(led_calcular(&e, 500), 0, 0, 0));
    VERIFICAR(igual(led_calcular(&e, 999), 0, 0, 0));
    VERIFICAR(igual(led_calcular(&e, 1000), LED_AMARILLO_R, LED_AMARILLO_G, 0));
    e.sin_conexion_ms = LED_REINTENTO_MS - 1;
    VERIFICAR(igual(led_calcular(&e, 0), LED_AMARILLO_R, LED_AMARILLO_G, 0));

    /* Sin conexión hace LED_REINTENTO_MS o más: rojo fijo (no parpadea) */
    e.sin_conexion_ms = LED_REINTENTO_MS;
    for (uint64_t t = 0; t < 3000; t += 100) {
        VERIFICAR(igual(led_calcular(&e, t), LED_ROJO_R, 0, 0));
    }
    e.sin_conexion_ms = 10 * LED_REINTENTO_MS;
    VERIFICAR(igual(led_calcular(&e, 700), LED_ROJO_R, 0, 0));

    /* Al volver la conexión pasa a verde enseguida, aunque el contador siga alto */
    e.conectada = true;
    VERIFICAR(igual(led_calcular(&e, 700), 0, LED_VERDE_G, 0));

    /* Calentando: azul que respira, nunca apagado del todo ni fuera de rango */
    e = (led_entrada_t){0};
    e.calentando = true;
    VERIFICAR(igual(led_calcular(&e, 0), 0, 0, LED_AZUL_MIN));
    VERIFICAR(igual(led_calcular(&e, LED_RESPIRAR_MS / 2), 0, 0, LED_AZUL_MAX));
    VERIFICAR(igual(led_calcular(&e, LED_RESPIRAR_MS), 0, 0, LED_AZUL_MIN));
    for (uint64_t t = 0; t < 2 * LED_RESPIRAR_MS; t += 37) {
        led_rgb_t c = led_calcular(&e, t);
        VERIFICAR(c.r == 0 && c.g == 0 && c.b >= LED_AZUL_MIN && c.b <= LED_AZUL_MAX);
    }
    /* Calentando tiene prioridad sobre el estado de conexión */
    e.conectada = true;
    VERIFICAR(led_calcular(&e, 500).g == 0);

    /* Falla de sensor: dos destellos cian cada 2 s, el resto del tiempo se ve la conexión */
    e = (led_entrada_t){0};
    e.sensor_falla = true;
    e.conectada = true;
    VERIFICAR(igual(led_calcular(&e, 0), 0, LED_CIAN_G, LED_CIAN_B));
    VERIFICAR(igual(led_calcular(&e, 149), 0, LED_CIAN_G, LED_CIAN_B));
    VERIFICAR(igual(led_calcular(&e, 150), 0, LED_VERDE_G, 0));
    VERIFICAR(igual(led_calcular(&e, 300), 0, LED_CIAN_G, LED_CIAN_B));
    VERIFICAR(igual(led_calcular(&e, 449), 0, LED_CIAN_G, LED_CIAN_B));
    VERIFICAR(igual(led_calcular(&e, 450), 0, LED_VERDE_G, 0));
    VERIFICAR(igual(led_calcular(&e, 1500), 0, LED_VERDE_G, 0));
    VERIFICAR(igual(led_calcular(&e, LED_FALLA_CICLO_MS), 0, LED_CIAN_G, LED_CIAN_B));
    e.conectada = false;                        /* sin conexión: entre destellos sigue el rojo */
    e.sin_conexion_ms = LED_REINTENTO_MS;
    VERIFICAR(igual(led_calcular(&e, 1000), LED_ROJO_R, 0, 0));
    /* Calentando y alarma pasan por encima de la falla */
    e.calentando = true;
    VERIFICAR(led_calcular(&e, 0).b >= LED_AZUL_MIN && led_calcular(&e, 0).g == 0);
    e.calentando = false;
    e.alarma = true;
    VERIFICAR(igual(led_calcular(&e, 0), LED_MAGENTA_R, 0, LED_MAGENTA_B));
    /* Sin falla ni otro estado, no hay cian */
    e = (led_entrada_t){0};
    e.conectada = true;
    VERIFICAR(igual(led_calcular(&e, 0), 0, LED_VERDE_G, 0));

    /* Alarma: magenta parpadeo rápido, con prioridad sobre todo */
    e.alarma = true;
    VERIFICAR(igual(led_calcular(&e, 0), LED_MAGENTA_R, 0, LED_MAGENTA_B));
    VERIFICAR(igual(led_calcular(&e, LED_ALARMA_MEDIO_MS), 0, 0, 0));
    VERIFICAR(igual(led_calcular(&e, 2 * LED_ALARMA_MEDIO_MS), LED_MAGENTA_R, 0, LED_MAGENTA_B));

    printf("ok\n");
    return 0;
}
