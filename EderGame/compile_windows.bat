@echo off
setlocal

rem --- Configuración ---
set BUILD_DIR=build
set BUILD_TYPE=Debug

echo [INFO] Compilando EderGraphics para Windows
echo.

rem --- Configuración de CMake con Ninja ---
cmake -S . -B %BUILD_DIR% ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

if %ERRORLEVEL% neq 0 (
    echo [FAILED] cmake configure
    exit /b 1
)

rem --- Compilación ---
cmake --build %BUILD_DIR%

if %ERRORLEVEL% neq 0 (
    echo [FAILED] cmake build
    exit /b 1
)

echo.
echo [OK] EderGraphics.dll generado en %BUILD_DIR%\
endlocal
PAUSE