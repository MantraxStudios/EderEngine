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

if (WIN32)
    set(VENDOR_LIBS
        ${VENDOR_LIB_DIR}/SDL3.lib
        ${VENDOR_LIB_DIR}/assimp-vc143-mt.lib
        ${VENDOR_LIB_DIR}/vulkan-1.lib
    )
endif()

# Android: compilar assimp desde fuente para el ABI actual
if (ANDROID)
    include(FetchContent)
    FetchContent_Declare(
        assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG        v5.3.1
    )
    set(ASSIMP_BUILD_TESTS        OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_SAMPLES      OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL            OFF CACHE BOOL "" FORCE)
    set(ASSIMP_NO_EXPORT          ON  CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS         ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(assimp)
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
        VULKAN_HPP_NO_SPACESHIP_OPERATOR
    )

    # SDL3 prebuilt — extracted from SDL3-3.4.2.aar by compile_android.bat
    set(SDL3_SO "${CMAKE_SOURCE_DIR}/vendor/androidlib/${ANDROID_ABI}/libSDL3.so")
    if (NOT EXISTS "${SDL3_SO}")
        message(FATAL_ERROR "libSDL3.so not found at ${SDL3_SO}. Run compile_android.bat to extract it from the AAR first.")
    endif()

    add_library(SDL3 SHARED IMPORTED)
    set_target_properties(SDL3 PROPERTIES IMPORTED_LOCATION "${SDL3_SO}")

    target_link_libraries(EderGraphics PRIVATE
        SDL3
        vulkan
        android
        log
    )

    target_link_libraries(EderGraphics PRIVATE assimp)

endif()