idf_component_register(SRCS "main.c" "almacen.c" "rele.c" "sonido.c" "lector.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nodo_mesh nvs_flash esp_system esp_driver_gpio esp_driver_ledc
                                     esp_driver_spi esp_driver_usb_serial_jtag esp_timer esp_event
                                     abobija__rc522)
