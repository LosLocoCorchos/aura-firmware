cmake_minimum_required(VERSION 3.16)
# Componentes comunes de la mesh de AURA (trama, biblioteca de la hoja y actualización por la malla)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../Mesh_Red_Completa/componentes")
# Tipo y versión del firmware (regla en Mesh_Red_Completa/GUIA_NODOS_HOJA.md, sección 2.7). El tipo es
# project(aura-<uso>): todas las placas de este uso llevan el mismo. Subir la versión en cada entrega.
set(PROJECT_VER "1.0.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aura-gas-co2)
