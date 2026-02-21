cmake_minimum_required(VERSION 3.16)

# The project is built for ESP32-S3
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(biomesh_field_node_mvp)
