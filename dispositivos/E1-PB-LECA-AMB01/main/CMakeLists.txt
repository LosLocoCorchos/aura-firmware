idf_component_register(SRCS "main.c" "scd4x.c" "sensirion.c" "mq2.c" "mq2_calc.c"
                            "alarma.c" "calentamiento.c" "led_patron.c" "led.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nodo_mesh nvs_flash esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_adc esp_timer)
