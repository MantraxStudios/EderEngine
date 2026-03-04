@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set GLSLC=glslc
set SHADER_DIR=shaders
set COUNT=0
set FAIL=0

for %%E in (vert frag comp geom tesc tese) do (
    for %%F in (%SHADER_DIR%\*.%%E) do (
        if exist "%%F" (
            %GLSLC% "%%F" -o "%%F.spv"
            if !errorlevel! equ 0 (
                echo [OK]      %%F  -^>  %%F.spv
                set /a COUNT+=1
            ) else (
                echo [FAILED]  %%F
                set /a FAIL+=1
            )
        )
    )
)

echo.
if !FAIL! equ 0 (
    echo Compiled !COUNT! shader(s) successfully.
    pause
) else (
    echo !FAIL! shader(s) failed to compile.
    exit /b 1
)

PAUSE