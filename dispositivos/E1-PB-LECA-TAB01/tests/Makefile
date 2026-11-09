# Tests de host de la lógica del PM2130 (main/pm2130.c), sin placa ni ESP-IDF.
# El CI de aura-firmware corre "make -C dispositivos/<CÓDIGO>/tests".
CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -I../main

test: test_pm2130 test_pm2130_todo
	./test_pm2130
	./test_pm2130_todo

# Con la configuración por defecto (solo los obligatorios de la fase A)
test_pm2130: test_pm2130.c ../main/pm2130.c ../main/pm2130.h
	$(CC) $(CFLAGS) -o $@ test_pm2130.c ../main/pm2130.c -lm

# Con todos los opcionales encendidos, para que esa parte también compile y se pruebe
test_pm2130_todo: test_pm2130.c ../main/pm2130.c ../main/pm2130.h
	$(CC) $(CFLAGS) -DENVIAR_FASES_BC=1 -DENVIAR_THD=1 -o $@ test_pm2130.c ../main/pm2130.c -lm

clean:
	rm -f test_pm2130 test_pm2130_todo

.PHONY: test clean
