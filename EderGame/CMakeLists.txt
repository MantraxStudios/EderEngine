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
set(EDERCORE_DIR ${CMAKE_SOURCE_DIR}/../EderCore)
set(EDERGRAPHICS_DIR ${CMAKE_SOURCE_DIR}/../EderGraphics)
set(PHYSX_INCLUDE ${EDERCORE_DIR}/vendor/include)
set(PHYSX_LIB_DIR ${CMAKE_SOURCE_DIR}/vendor/lib)
set(PHYSX_DLL_DIR ${CMAKE_SOURCE_DIR}/vendor/lib)
set(EDERGRAPHICS_INCLUDE ${EDERGRAPHICS_DIR}/src)
set(EDERGRAPHICS_VENDOR_INCLUDE ${EDERGRAPHICS_DIR}/vendor/include)
set(EDERGRAPHICS_VENDOR_LIB ${EDERGRAPHICS_DIR}/vendor/lib)
set(EDERGRAPHICS_LIB ${EDERGRAPHICS_DIR}/build/EderGraphics.lib) # Windows Debug
set(EDERGRAPHICS_DLL ${EDERGRAPHICS_DIR}/build/EderGraphics.dll)

# Fuentes
file(GLOB_RECURSE GAME_SOURCES src/*.cpp)
# Excluir fuentes del player standalone de EderGame
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/Player/.*")

# ImGui sources (compilados directamente en EderGame)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/include/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/imgui_impl_vulkan.cpp
    ${IMGUI_DIR}/ImGuizmo.cpp
)

# --------------------------------------------------
# Crear ejecutable
# --------------------------------------------------
add_executable(EderGame ${GAME_SOURCES} ${IMGUI_SOURCES})

# --------------------------------------------------
# Includes
# --------------------------------------------------
target_include_directories(EderGame PRIVATE
    src
    ${EDERCORE_DIR}/src
    ${EDERGRAPHICS_INCLUDE}
    ${EDERGRAPHICS_VENDOR_INCLUDE}
    ${CMAKE_SOURCE_DIR}/vendor/include
    ${PHYSX_INCLUDE}
)

# --------------------------------------------------
# Definiciones comunes
# --------------------------------------------------
target_compile_definitions(EderGame PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    PX_SIMD_DISABLED
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
        ${EDERGRAPHICS_VENDOR_LIB}/SDL3.lib
        ${EDERGRAPHICS_VENDOR_LIB}/vulkan-1.lib
        ${PHYSX_LIB_DIR}/PhysX_64.lib
        ${PHYSX_LIB_DIR}/PhysXCommon_64.lib
        ${PHYSX_LIB_DIR}/PhysXFoundation_64.lib
        ${PHYSX_LIB_DIR}/PhysXExtensions_static_64.lib
        ${PHYSX_LIB_DIR}/PhysXPvdSDK_static_64.lib
        ${PHYSX_LIB_DIR}/PhysXCooking_64.lib
    )

    target_compile_definitions(EderGame PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        NOMINMAX
    )

    # Copiar EderGraphics.dll
    add_custom_command(TARGET EderGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EDERGRAPHICS_DLL}"
        $<TARGET_FILE_DIR:EderGame>
        COMMENT "Copiando EderGraphics.dll"
    )

    # Copiar SDL3.dll
    add_custom_command(TARGET EderGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EDERGRAPHICS_VENDOR_LIB}/SDL3.dll"
        $<TARGET_FILE_DIR:EderGame>
        COMMENT "Copiando SDL3.dll"
    )

    # Copiar PhysX DLLs
    foreach(PHYSX_DLL PhysX_64.dll PhysXCommon_64.dll PhysXFoundation_64.dll PhysXCooking_64.dll)
        add_custom_command(TARGET EderGame POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PHYSX_DLL_DIR}/${PHYSX_DLL}"
            $<TARGET_FILE_DIR:EderGame>
            COMMENT "Copiando ${PHYSX_DLL}"
        )
    endforeach()
endif()

# --------------------------------------------------
# EderPlayer — runtime de juego standalone (sin ImGui)
# --------------------------------------------------
set(PLAYER_SOURCES
    src/Player/PlayerMain.cpp
    src/Player/PlayerApp.cpp
)

add_executable(EderPlayer ${PLAYER_SOURCES})

target_include_directories(EderPlayer PRIVATE
    src
    ${EDERCORE_DIR}/src
    ${EDERGRAPHICS_INCLUDE}
    ${EDERGRAPHICS_VENDOR_INCLUDE}
    ${CMAKE_SOURCE_DIR}/vendor/include
    ${PHYSX_INCLUDE}
)

target_compile_definitions(EderPlayer PRIVATE
    EDER_PLAYER
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    PX_SIMD_DISABLED
)

if (MSVC)
    target_compile_options(EderPlayer PRIVATE
        /EHsc
        /W0
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(EderPlayer PRIVATE
        -fexceptions
        -w
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O2>
    )
endif()

if (WIN32)
    target_link_libraries(EderPlayer PRIVATE
        ${EDERGRAPHICS_LIB}
        ${EDERGRAPHICS_VENDOR_LIB}/SDL3.lib
        ${EDERGRAPHICS_VENDOR_LIB}/vulkan-1.lib
        ${PHYSX_LIB_DIR}/PhysX_64.lib
        ${PHYSX_LIB_DIR}/PhysXCommon_64.lib
        ${PHYSX_LIB_DIR}/PhysXFoundation_64.lib
        ${PHYSX_LIB_DIR}/PhysXExtensions_static_64.lib
        ${PHYSX_LIB_DIR}/PhysXPvdSDK_static_64.lib
        ${PHYSX_LIB_DIR}/PhysXCooking_64.lib
    )

    target_compile_definitions(EderPlayer PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        NOMINMAX
    )

    add_custom_command(TARGET EderPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EDERGRAPHICS_DLL}"
        $<TARGET_FILE_DIR:EderPlayer>
        COMMENT "Copiando EderGraphics.dll para EderPlayer"
    )

    add_custom_command(TARGET EderPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EDERGRAPHICS_VENDOR_LIB}/SDL3.dll"
        $<TARGET_FILE_DIR:EderPlayer>
        COMMENT "Copiando SDL3.dll para EderPlayer"
    )

    # Copiar PhysX DLLs para EderPlayer
    foreach(PHYSX_DLL PhysX_64.dll PhysXCommon_64.dll PhysXFoundation_64.dll PhysXCooking_64.dll)
        add_custom_command(TARGET EderPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PHYSX_DLL_DIR}/${PHYSX_DLL}"
            $<TARGET_FILE_DIR:EderPlayer>
            COMMENT "Copiando ${PHYSX_DLL} para EderPlayer"
        )
    endforeach()
endif()