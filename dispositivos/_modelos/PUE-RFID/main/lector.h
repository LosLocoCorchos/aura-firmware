#pragma once
/*
 * Lector RC522 (SPI) sobre el componente abobija/rc522 (version fija en idf_component.yml).
 * Cada vez que se apoya un llavero, deja su UID en la cola que se le pasa. El mismo codigo
 * lo usa el lector de banco (lector_banco/).
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lector_logica.h"
#include "lista_tarjetas.h"

typedef struct {
    uint8_t largo;
    uint8_t uid[UID_MAX_BYTES];
} lectura_t;

/* Arranca el SPI y el lector. Cada lectura va a `cola` (de elementos lectura_t). */
esp_err_t lector_iniciar(QueueHandle_t cola);

/* Revisa que el RC522 siga vivo y leyendo, y si no, lo levanta. Si rc522_start() no se completo
   (cable flojo al encender) lo reintenta: mientras no lea, no es LECTOR_OK. Si ademas se
   reinicio (version y antena), lo reinicia. Llamar cada unos segundos desde una tarea que no
   sea la del lector. Las reglas estan en lector_logica.h. */
lector_salud_t lector_revisar(void);
