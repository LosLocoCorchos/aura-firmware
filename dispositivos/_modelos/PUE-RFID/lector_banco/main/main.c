/*
 * Lector de banco para dar de alta llaveros: apoyas el llavero y escribe su UID por USB.
 *
 *     UID ABE8B022
 *
 * Es el texto que se le pasa a AURA para el alta (A1). Sale del MISMO codigo que usa la puerta
 * (lector.c para leer, lista_tarjetas.h para escribir el UID: hexa en mayusculas, sin
 * separadores, de 4, 7 o 10 bytes), asi que lo que ves aca es lo que la puerta va a comparar.
 *
 * No habla por la mesh, ni por ESP-NOW, ni abre nada, ni puede cambiar la lista de ninguna
 * puerta: solo lee. La lista la administra AURA con tarjeta_agregar / tarjeta_quitar.
 *
 * Pines: los mismos que la puerta (main/pines.h de la puerta).
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "lector.h"
#include "lista_tarjetas.h"

void app_main(void)
{
    QueueHandle_t cola = xQueueCreate(4, sizeof(lectura_t));
    esp_err_t e = lector_iniciar(cola);
    if (e != ESP_OK) {
        printf("ERROR: el RC522 no responde (%s). Revisa el cableado.\n", esp_err_to_name(e));
    } else {
        printf("Lector listo: apoya un llavero.\n");
    }
    while (true) {
        lectura_t l;
        if (xQueueReceive(cola, &l, pdMS_TO_TICKS(5000)) == pdTRUE) {
            char hex[UID_HEX_MAX];
            uid_a_hex(l.uid, l.largo, hex);
            printf("UID %s\n", hex);
        } else if (e == ESP_OK && lector_revisar() == LECTOR_CAIDO) {
            printf("ERROR: el RC522 dejo de responder.\n");
        }
    }
}
