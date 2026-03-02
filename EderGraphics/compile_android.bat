@echo off
setlocal

set NDK=C:\Users\tupap\AppData\Local\Android\Sdk\ndk\29.0.14206865
set PLATFORM=android-33
set BUILD_TYPE=Release
set JNILIBS=C:\Users\tupap\AndroidStudioProjects\EderEngineTest\app\src\main\jniLibs
set AAR=%~dp0vendor\androidlib\SDL3-3.4.2.aar
set GLSLC=%NDK%\shader-tools\windows-x86_64\glslc.exe
set SHADER_SRC=C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderGame\build\shaders
set ASSETS_SHADERS=C:\Users\tupap\AndroidStudioProjects\EderEngineTest\app\src\main\assets\shaders

echo [INFO] Extrayendo libSDL3.so del AAR...
powershell -NoProfile -Command ^
  "Add-Type -AssemblyName System.IO.Compression.FileSystem; $zip = [System.IO.Compression.ZipFile]::OpenRead('%AAR%'); foreach ($e in $zip.Entries) { if ($e.FullName -match 'prefab/modules/SDL3-shared/libs/android\.(.*)/libSDL3\.so') { $abi = $matches[1]; $dest = '%~dp0vendor\androidlib\' + $abi; if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Path $dest | Out-Null }; $out = $dest + '\libSDL3.so'; $s = $e.Open(); $f = [System.IO.File]::Create($out); $s.CopyTo($f); $f.Close(); $s.Close(); Write-Host ('  [OK] ' + $abi + '/libSDL3.so') } }; $zip.Dispose()"

if errorlevel 1 (
    echo [FAILED] No se pudo extraer libSDL3.so del AAR
    exit /b 1
)

echo.
echo [INFO] Compilando shaders con glslc...
if not exist "%ASSETS_SHADERS%" mkdir "%ASSETS_SHADERS%"

for %%S in (skybox triangle shadow point_shadow shadow_debug debug) do (
    if exist "%SHADER_SRC%\%%S.vert" (
        "%GLSLC%" "%SHADER_SRC%\%%S.vert" -o "%ASSETS_SHADERS%\%%S.vert.spv"
        if errorlevel 1 ( echo [FAILED] %%S.vert & exit /b 1 )
        echo   [OK] %%S.vert.spv
    )
    if exist "%SHADER_SRC%\%%S.frag" (
        "%GLSLC%" "%SHADER_SRC%\%%S.frag" -o "%ASSETS_SHADERS%\%%S.frag.spv"
        if errorlevel 1 ( echo [FAILED] %%S.frag & exit /b 1 )
        echo   [OK] %%S.frag.spv
    )
)
echo [INFO] Shaders compilados en %ASSETS_SHADERS%

for %%A in (armeabi-v7a arm64-v8a x86_64 x86) do (
    echo.
    echo [INFO] Compilando para %%A ...

    cmake -S . -B build-android-%%A ^
        -G Ninja ^
        -DCMAKE_TOOLCHAIN_FILE="%NDK%\build\cmake\android.toolchain.cmake" ^
        -DANDROID_ABI=%%A ^
        -DANDROID_PLATFORM=%PLATFORM% ^
        -DANDROID_STL=c++_static ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

    if errorlevel 1 (
        echo [FAILED] cmake configure para %%A
        exit /b 1
    )

    cmake --build build-android-%%A

    if errorlevel 1 (
        echo [FAILED] cmake build para %%A
        exit /b 1
    )

    if not exist "%JNILIBS%\%%A" mkdir "%JNILIBS%\%%A"
    copy /Y build-android-%%A\libEderGraphics.so "%JNILIBS%\%%A\libEderGraphics.so"
    copy /Y build-android-%%A\_deps\assimp-build\bin\libassimp.so "%JNILIBS%\%%A\libassimp.so" 2>nul
    copy /Y vendor\androidlib\%%A\libSDL3.so "%JNILIBS%\%%A\libSDL3.so"

    echo [OK] %%A listo en %JNILIBS%\%%A\
)

echo.
echo [OK] Todos los ABIs compilados y copiados a jniLibs.
endlocal
PAUSE
