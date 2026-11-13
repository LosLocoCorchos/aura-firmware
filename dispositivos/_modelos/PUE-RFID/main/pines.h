#pragma once
/*
 * Pines de la ESP32-C3 SuperMini. Son los mismos en las nueve puertas (el cableado es igual);
 * lo que cambia de una puerta a otra va en set_config, no aca.
 *
 * Ojo con los pines de strapping de la C3 (GPIO2, GPIO8 y GPIO9): si algo los baja al
 * arrancar, la placa entra en modo descarga o arranca distinto. Por eso el SCK del RC522 NO
 * va en el GPIO9 (pin de BOOT), que era donde estaba en el prototipo de banco.
 */
#include "driver/gpio.h"

/* Lector RC522 (SPI) */
#define PIN_RC522_SCK   GPIO_NUM_6
#define PIN_RC522_MISO  GPIO_NUM_10
#define PIN_RC522_MOSI  GPIO_NUM_20
#define PIN_RC522_SS    GPIO_NUM_21
#define PIN_RC522_RST   GPIO_NUM_7

/* Buzzer pasivo (bobina) directo al pin, con PWM */
#define PIN_BUZZER      GPIO_NUM_3

/* Sensor de la puerta (MS14): entre el pin y GND, con pull-up interna */
#define PIN_SENSOR      GPIO_NUM_4

/* Modulo de rele de la cerradura. En reposo el rele tiene que estar SUELTO (la cerradura sin
   corriente queda TRABADA). Hay modulos que se activan con el pin en BAJO y otros con el pin en
   ALTO (o con un jumper H/L): se elige con CONFIG_PUERTA_RELE_ACTIVO_ALTO (Kconfig). Si queda
   mal elegido el rele queda activado en reposo y la cerradura destrabada y energizada: se nota
   porque el LED del rele esta prendido sin que nadie haya pasado un llavero. */
#define PIN_RELE        GPIO_NUM_5
#ifdef CONFIG_PUERTA_RELE_ACTIVO_ALTO
#define RELE_ACTIVO_BAJO 0
#else
#define RELE_ACTIVO_BAJO 1
#endif
