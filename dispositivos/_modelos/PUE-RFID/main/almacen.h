#pragma once
/*
 * Lo que la puerta guarda en NVS (namespace "puerta"): la lista de llaveros, si esta armada y
 * su configuracion. Regla del repo y del review: se escribe SOLO si el valor cambio, para no
 * gastar la flash (un latido o un set_config repetido no escriben nada).
 */
#include <stdbool.h>
#include <stdint.h>
#include "lista_tarjetas.h"
#include "sensor_logica.h"

typedef struct {
    int32_t pulso_ms;
    int32_t aviso_s;
    sensor_tipo_t sensor;
} config_puerta_t;

bool almacen_iniciar(void);

/* Si no hay nada guardado devuelve la lista vacia (placa nueva: no abre con ningun llavero) */
void almacen_cargar_lista(lista_t *l);
bool almacen_guardar_lista(const lista_t *l);

bool almacen_cargar_armada(void);
bool almacen_guardar_armada(bool armada);

/* La config se guarda de una sola vez (un blob, un commit): o queda la nueva entera o la vieja
   entera. Si lo guardado no es valido, la carga devuelve los valores por defecto de los tres. */
void almacen_cargar_config(config_puerta_t *c);
bool almacen_guardar_config(const config_puerta_t *c);
