cmake_minimum_required(VERSION 3.21)

project(EderGraphics VERSION 0.1 LANGUAGES CXX)

# --------------------------------------------------
# Configuración general
# --------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/vendor)
set(VENDOR_INCLUDE_DIR ${VENDOR_DIR}/include)
set(VENDOR_LIB_DIR ${VENDOR_DIR}/lib)

file(GLOB_RECURSE SOURCES src/*.cpp)

# Solo usar .lib en Windows (libs explícitas para evitar conflictos con múltiples variantes de GLFW)
if (WIN32)
    set(VENDOR_LIBS
        ${VENDOR_LIB_DIR}/glfw3dll.lib
        ${VENDOR_LIB_DIR}/assimp-vc143-mt.lib
        ${VENDOR_LIB_DIR}/vulkan-1.lib
    )
endif()

# Android: usar libassimp.so precompilada de vendor/androidlib
if (ANDROID)
    add_library(assimp SHARED IMPORTED)
    set_target_properties(assimp PROPERTIES
        IMPORTED_LOCATION ${VENDOR_DIR}/androidlib/libassimp.so
    )
endif()

# --------------------------------------------------
# Crear biblioteca compartida (.dll / .so)
# --------------------------------------------------

add_library(EderGraphics SHARED ${SOURCES})

# --------------------------------------------------
# Includes
# --------------------------------------------------

target_include_directories(EderGraphics PRIVATE
    src
    ${VENDOR_INCLUDE_DIR}
)

# --------------------------------------------------
# Definiciones comunes
# --------------------------------------------------

target_compile_definitions(EderGraphics PRIVATE
    EDERGRAPHICS_EXPORTS
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

# --------------------------------------------------
# Opciones de compilación
# --------------------------------------------------

if (MSVC)
    target_compile_options(EderGraphics PRIVATE
        /EHsc
        /W0
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(EderGraphics PRIVATE
        -fexceptions
        -w
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O3>
    )
endif()

# --------------------------------------------------
# Plataforma: Windows
# --------------------------------------------------

if (WIN32)

    target_compile_definitions(EderGraphics PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        NOMINMAX
    )

    target_link_libraries(EderGraphics PRIVATE
        ${VENDOR_LIBS}
        user32
        gdi32
        shell32
        ole32
        advapi32
    )

endif()

# --------------------------------------------------
# Plataforma: Android
# --------------------------------------------------

if (ANDROID)

    target_compile_definitions(EderGraphics PRIVATE
        VK_USE_PLATFORM_ANDROID_KHR
    )

    target_link_libraries(EderGraphics PRIVATE
        vulkan
        android
        log
        assimp
    )

endif()