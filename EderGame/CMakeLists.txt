cmake_minimum_required(VERSION 3.21)

project(EderGame VERSION 0.1 LANGUAGES CXX)

# --------------------------------------------------
# Configuración general
# --------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Forzar LLVM/Clang si quieres (opcional)
# set(CMAKE_C_COMPILER "clang")
# set(CMAKE_CXX_COMPILER "clang++")
# set(CMAKE_MAKE_PROGRAM "ninja")

# Paths
set(EDERGRAPHICS_DIR ${CMAKE_SOURCE_DIR}/../EderGraphics)
set(EDERGRAPHICS_INCLUDE ${EDERGRAPHICS_DIR}/src)
set(EDERGRAPHICS_VENDOR_INCLUDE ${EDERGRAPHICS_DIR}/vendor/include)
set(EDERGRAPHICS_VENDOR_LIB ${EDERGRAPHICS_DIR}/vendor/lib)
set(EDERGRAPHICS_LIB ${EDERGRAPHICS_DIR}/build/EderGraphicsd.lib) # Windows Debug
set(EDERGRAPHICS_DLL ${EDERGRAPHICS_DIR}/build/EderGraphicsd.dll)

# Fuentes
file(GLOB_RECURSE GAME_SOURCES src/*.cpp)

# --------------------------------------------------
# Crear ejecutable
# --------------------------------------------------
add_executable(EderGame ${GAME_SOURCES})

# --------------------------------------------------
# Includes
# --------------------------------------------------
target_include_directories(EderGame PRIVATE
    src
    ${EDERGRAPHICS_INCLUDE}
    ${EDERGRAPHICS_VENDOR_INCLUDE}
)

# --------------------------------------------------
# Definiciones comunes
# --------------------------------------------------
target_compile_definitions(EderGame PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

# --------------------------------------------------
# Opciones de compilación
# --------------------------------------------------
if (MSVC)
    target_compile_options(EderGame PRIVATE
        /EHsc
        /W0
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(EderGame PRIVATE
        -fexceptions
        -w
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O2>
    )
endif()

# --------------------------------------------------
# Link con librerías de EderGraphics
# --------------------------------------------------
if (WIN32)
    target_link_libraries(EderGame PRIVATE
        ${EDERGRAPHICS_LIB}
        ${EDERGRAPHICS_VENDOR_LIB}/glfw3dll.lib
    )

    target_compile_definitions(EderGame PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        NOMINMAX
    )

    # Copiar EderGraphicsd.dll
    add_custom_command(TARGET EderGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EDERGRAPHICS_DLL}"
        $<TARGET_FILE_DIR:EderGame>
        COMMENT "Copiando EderGraphicsd.dll"
    )

    # Copiar glfw3.dll (GLFW compartido entre EXE y DLL)
    add_custom_command(TARGET EderGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EDERGRAPHICS_VENDOR_LIB}/glfw3.dll"
        $<TARGET_FILE_DIR:EderGame>
        COMMENT "Copiando glfw3.dll"
    )
endif()