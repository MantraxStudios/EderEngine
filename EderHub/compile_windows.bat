@echo off
setlocal
title EderHub — Build

set BUILD_TYPE=Debug
set ROOT=%~dp0..

echo.
echo ============================================================
echo  1/3  EderGraphics
echo ============================================================
cmake -S "%ROOT%\EderGraphics" -B "%ROOT%\EderGraphics\build" ^
    -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %ERRORLEVEL% neq 0 (echo [FAILED] cmake configure EderGraphics & goto :fail)
cmake --build "%ROOT%\EderGraphics\build"
if %ERRORLEVEL% neq 0 (echo [FAILED] build EderGraphics & goto :fail)

echo.
echo ============================================================
echo  2/3  EderGame  +  EderPlayer
echo ============================================================
cmake -S "%ROOT%\EderGame" -B "%ROOT%\EderGame\build" ^
    -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %ERRORLEVEL% neq 0 (echo [FAILED] cmake configure EderGame & goto :fail)
cmake --build "%ROOT%\EderGame\build"
if %ERRORLEVEL% neq 0 (echo [FAILED] build EderGame & goto :fail)

echo.
echo ============================================================
echo  3/3  EderHub
echo ============================================================
cmake -S "%~dp0." -B "%~dp0build" ^
    -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %ERRORLEVEL% neq 0 (echo [FAILED] cmake configure EderHub & goto :fail)
cmake --build "%~dp0build"
if %ERRORLEVEL% neq 0 (echo [FAILED] build EderHub & goto :fail)

echo.
echo ============================================================
echo  [OK]  Listo!  Ejecutable: EderHub\build\EderHub.exe
echo ============================================================
echo.
PAUSE
endlocal
exit /b 0

:fail
echo.
echo [ERROR] La compilacion fallo. Revisa los mensajes de arriba.
PAUSE
endlocal
exit /b 1
