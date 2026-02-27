cmake_minimum_required(VERSION 3.21)

project(EderGraphics VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/vendor)
set(VENDOR_INCLUDE_DIR ${VENDOR_DIR}/include)
set(VENDOR_LIB_DIR ${VENDOR_DIR}/lib)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB VENDOR_LIBS ${VENDOR_LIB_DIR}/*.lib)

add_executable(EderGraphics ${SOURCES})

target_include_directories(EderGraphics PRIVATE
    src
    ${VENDOR_INCLUDE_DIR}
)

target_link_libraries(EderGraphics PRIVATE
    ${VENDOR_LIBS}
    user32
    gdi32
    shell32
    ole32
    advapi32
)

target_compile_definitions(EderGraphics PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if (MSVC)
    target_compile_options(EderGraphics PRIVATE
        /EHsc
        /W0
    )
else()
    target_compile_options(EderGraphics PRIVATE
        -fexceptions
        -w
    )
endif()

if (MSVC)
    target_compile_options(EderGraphics PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(EderGraphics PRIVATE
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O3>
    )
endif()

if (WIN32)
    target_compile_definitions(EderGraphics PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        NOMINMAX
    )
endif()