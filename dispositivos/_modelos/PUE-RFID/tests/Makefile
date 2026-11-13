CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -g

TESTS = test_lista test_cerradura test_puerta test_sonido test_latido test_sensor test_lector test_config

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do \
	  echo "== $$t =="; ./$$t || exit 1; \
	done

test_lista: test_lista.cpp ../main/lista_tarjetas.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_cerradura: test_cerradura.cpp ../main/cerradura_pulso.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_puerta: test_puerta.cpp ../main/puerta_logica.h ../main/cerradura_pulso.h ../main/lista_tarjetas.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_sonido: test_sonido.cpp ../main/sonido_logica.h ../main/puerta_logica.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_latido: test_latido.cpp ../main/latido_logica.h ../main/puerta_logica.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_sensor: test_sensor.cpp ../main/sensor_logica.h ../main/puerta_logica.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_lector: test_lector.cpp ../main/lector_logica.h
	$(CXX) $(CXXFLAGS) -o $@ $<

test_config: test_config.cpp ../main/config_validacion.h
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TESTS)
