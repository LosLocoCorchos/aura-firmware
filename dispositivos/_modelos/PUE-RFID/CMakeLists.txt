cmake_minimum_required(VERSION 3.16)
# Componentes comunes de la mesh de AURA. Esta carpeta esta un nivel mas abajo que un proyecto
# de dispositivo (dispositivos/_modelos/PUE-RFID), de ahi el ../../../
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../Mesh_Red_Completa/componentes")
# Tipo y version del firmware (regla en Mesh_Red_Completa/GUIA_NODOS_HOJA.md, seccion 2.7).
# Todas las puertas llevan el mismo tipo y el mismo binario. Subir la version en cada entrega.
set(PROJECT_VER "0.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aura-puerta-rfid)
