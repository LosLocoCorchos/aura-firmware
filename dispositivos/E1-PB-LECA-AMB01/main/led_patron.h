/*
 * Colores y patrones del LED RGB de estado. Sin hardware: se prueba en la PC.
 *
 *   Verde fijo                    conectado a la mesh
 *   Amarillo parpadeando          intentando conectar (arranque, o sin conexión hace menos de LED_REINTENTO_MS)
 *   Rojo fijo                     sin conexión desde hace más de LED_REINTENTO_MS
 *   Azul "respirando"             calentando sensores
 *   Magenta parpadeo rápido       alarma de gas o CO2 (tiene prioridad sobre todo lo demás)
 *   Cian, dos destellos cortos    un sensor falla: se intercala con el color de la conexión
 *
 * Prioridad: alarma > calentando > falla de sensor > estado de la conexión. Los estados que no
 * corresponden se dejan en false en la entrada.
 * Los brillos (0..255) son los de abajo: ajustarlos a mano según cómo se vea el módulo.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LED_REINTENTO_MS   60000u    /* sin conexión por más de esto: rojo fijo en vez de amarillo */
#define LED_MEDIO_PERIODO_MS 500u    /* amarillo: 500 ms prendido, 500 ms apagado */
#define LED_ALARMA_MEDIO_MS  150u    /* magenta de alarma: 150 ms prendido, 150 ms apagado */
#define LED_RESPIRAR_MS     2000u    /* azul: período completo de una "respiración" */
#define LED_FALLA_CICLO_MS  2000u    /* cian: cada 2 s, dos destellos de 150 ms separados 150 ms */
#define LED_FALLA_DESTELLO_MS 150u

/* Brillos. Con un módulo con resistencias pensadas para 5 V y alimentado a 3,3 V, el verde y el
   azul salen más débiles que el rojo: si el amarillo se ve anaranjado, bajar AMARILLO_R; si se ve
   verdoso, bajarle AMARILLO_G. */
#define LED_ROJO_R         255
#define LED_VERDE_G        255
#define LED_AMARILLO_R     200
#define LED_AMARILLO_G     255
#define LED_AZUL_MIN        10
#define LED_AZUL_MAX       255
#define LED_MAGENTA_R      255
#define LED_MAGENTA_B      255
#define LED_CIAN_G         255
#define LED_CIAN_B         255

typedef struct {
    uint8_t r, g, b;
} led_rgb_t;

typedef struct {
    bool conectada;               /* nodo_mesh_conectada() */
    uint64_t sin_conexion_ms;     /* desde cuándo no está conectada (desde el arranque si nunca lo estuvo) */
    bool calentando;              /* opcional: solo si se quiere mostrar el calentamiento */
    bool alarma;                  /* opcional: solo si se quiere mostrar la alarma */
    bool sensor_falla;            /* algún sensor no da una medición válida (ya terminado el calentamiento) */
} led_entrada_t;

/* Color que corresponde en el instante ahora_ms (tiempo desde el arranque). */
led_rgb_t led_calcular(const led_entrada_t *e, uint64_t ahora_ms);
