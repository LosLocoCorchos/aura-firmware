cmake_minimum_required(VERSION 3.16)
# Componentes comunes de la mesh de AURA (trama y biblioteca de la hoja)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../Mesh_Red_Completa/componentes")
# Tipo y versión del firmware (GUIA_NODOS_HOJA.md, 2.7): la versión se sube en CADA entrega
set(PROJECT_VER "1.1.1")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aura-energia-pm2130)
