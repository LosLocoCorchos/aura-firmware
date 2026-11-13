cmake_minimum_required(VERSION 3.16)
# Lector de enrolamiento de banco: C3 + RC522 por USB. Comparte con la puerta el lector y el
# formato del UID (main/lector.c y main/lista_tarjetas.h) para que den EXACTAMENTE el mismo texto.
set(PROJECT_VER "0.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aura-lector-banco)
