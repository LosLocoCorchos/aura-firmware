idf_component_register(
    SRCS "main.c" "logica.c" "config_parser.c" "configuracion.c"
         "almacenamiento.c" "sondas.c" "bus_i2c.c" "reloj.c" "pantalla.c" "energia.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nodo_mesh nvs_flash esp_driver_gpio esp_driver_i2c esp_timer
                  espressif__ds18b20 espressif__onewire_bus espressif__cjson)
