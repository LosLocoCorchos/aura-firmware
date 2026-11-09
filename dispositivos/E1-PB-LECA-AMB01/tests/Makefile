# Tests de host de E1-PB-LECA-AMB01: la lógica que no toca hardware (protocolo del SCD40,
# conversión del MQ2, alarma, sondas, calentamiento y colores del LED). El CI corre `make` en esta carpeta.
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -I../main

TESTS = test_sensirion test_mq2 test_alarma test_calentamiento test_led_patron test_sonda

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do \
	  echo "== $$t =="; ./$$t || exit 1; \
	done

test_sensirion: test_sensirion.c ../main/sensirion.c ../main/sensirion.h aserciones.h
	$(CC) $(CFLAGS) -o $@ test_sensirion.c ../main/sensirion.c -lm

test_mq2: test_mq2.c ../main/mq2_calc.c ../main/mq2_calc.h aserciones.h
	$(CC) $(CFLAGS) -o $@ test_mq2.c ../main/mq2_calc.c

test_alarma: test_alarma.c ../main/alarma.c ../main/alarma.h aserciones.h
	$(CC) $(CFLAGS) -o $@ test_alarma.c ../main/alarma.c

test_calentamiento: test_calentamiento.c ../main/calentamiento.c ../main/calentamiento.h aserciones.h
	$(CC) $(CFLAGS) -o $@ test_calentamiento.c ../main/calentamiento.c

test_led_patron: test_led_patron.c ../main/led_patron.c ../main/led_patron.h aserciones.h
	$(CC) $(CFLAGS) -o $@ test_led_patron.c ../main/led_patron.c

test_sonda: test_sonda.c ../main/sonda.c ../main/sonda.h aserciones.h
	$(CC) $(CFLAGS) -o $@ test_sonda.c ../main/sonda.c

clean:
	rm -f $(TESTS)
