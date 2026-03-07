cmake_minimum_required(VERSION 3.21)
project(EderHub VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Paths ────────────────────────────────────────────────────────────────────
set(EDERGAME_DIR          ${CMAKE_SOURCE_DIR}/../EderGame)
set(EDERGAME_BUILD        ${EDERGAME_DIR}/build)
set(EDERGRAPHICS_DIR      ${CMAKE_SOURCE_DIR}/../EderGraphics)
set(SDL_VENDOR_LIB        ${EDERGRAPHICS_DIR}/vendor/lib)
set(SDL_VENDOR_INC        ${EDERGRAPHICS_DIR}/vendor/include)
set(IMGUI_DIR             ${EDERGAME_DIR}/vendor/include/imgui)

# ── Sources ──────────────────────────────────────────────────────────────────
set(HUB_SOURCES
    src/main.cpp
    src/HubApp.cpp
    src/imgui_impl_sdlrenderer3.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_sdl3.cpp
)

add_executable(EderHub ${HUB_SOURCES})

# ── Includes ─────────────────────────────────────────────────────────────────
target_include_directories(EderHub PRIVATE
    src
    ${IMGUI_DIR}/..       # resolves <imgui/imgui.h>
    ${SDL_VENDOR_INC}
)

# ── Definitions ──────────────────────────────────────────────────────────────
target_compile_definitions(EderHub PRIVATE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

# ── Compile options ───────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(EderHub PRIVATE /EHsc /W0
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2>)
else()
    target_compile_options(EderHub PRIVATE -fexceptions -w
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O2>)
endif()

# ── Link libraries ────────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(EderHub PRIVATE
        ${SDL_VENDOR_LIB}/SDL3.lib
        ole32
        shell32
    )
endif()

# ── Post-build: copy everything to the EderHub build output ──────────────────
if(WIN32)
    set(OUT $<TARGET_FILE_DIR:EderHub>)
    set(GAME_BIN ${EDERGAME_BUILD})

    # SDL3.dll
    add_custom_command(TARGET EderHub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL_VENDOR_LIB}/SDL3.dll" "${OUT}/SDL3.dll"
        COMMENT "Copiando SDL3.dll")

    # EderGame.exe + EderPlayer.exe
    foreach(BIN EderGame.exe EderPlayer.exe)
        add_custom_command(TARGET EderHub POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GAME_BIN}/${BIN}" "${OUT}/${BIN}"
            COMMENT "Copiando ${BIN}")
    endforeach()

    # EderGraphics.dll + lua54.dll + fmod.dll
    foreach(DLL EderGraphics.dll lua54.dll fmod.dll)
        add_custom_command(TARGET EderHub POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GAME_BIN}/${DLL}" "${OUT}/${DLL}"
            COMMENT "Copiando ${DLL}")
    endforeach()

    # shaders/ (actual path: EderGame/build/Game/Content/shaders)
    add_custom_command(TARGET EderHub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GAME_BIN}/Game/Content/shaders" "${OUT}/Game/Content/shaders"
        COMMENT "Copiando shaders")

    # fonts/
    add_custom_command(TARGET EderHub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GAME_BIN}/Game/Content/fonts" "${OUT}/Game/Content/fonts"
        COMMENT "Copiando fonts")

    # Ensure a default Game/Content tree exists so EderGame.exe works
    # when launched from EderHub/build/ without --workdir
    add_custom_command(TARGET EderHub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT}/Game/Content/scenes"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT}/Game/Content/scripts"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT}/Game/Content/materials"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT}/Game/Content/textures"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT}/Game/Content/meshes"
        COMMENT "Creando estructura Game/Content")
endif()
