cmake_minimum_required(VERSION 3.20)
project(EderCore)

add_library(EderCore INTERFACE)

target_include_directories(EderCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/EderGraphics/vendor/include   # glm
)
