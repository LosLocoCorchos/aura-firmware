# lector.c y pines.h son los de la puerta: el mismo codigo, para que el UID salga igual
idf_component_register(SRCS "main.c" "../../main/lector.c"
                       INCLUDE_DIRS "." "../../main"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_event abobija__rc522)
