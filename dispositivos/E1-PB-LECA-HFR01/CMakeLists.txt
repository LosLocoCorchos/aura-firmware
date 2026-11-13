cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../Mesh_Red_Completa/componentes")
# Identidad del firmware por uso, independiente del codigo de la instalacion.
set(PROJECT_VER "1.0.2")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aura-heladera-freezer)
