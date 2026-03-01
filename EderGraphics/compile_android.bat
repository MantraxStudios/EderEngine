@echo off
setlocal

set NDK=C:\Users\tupap\AppData\Local\Android\Sdk\ndk\29.0.14206865
set PLATFORM=android-33
set BUILD_TYPE=Release
set JNILIBS=C:\Users\tupap\AndroidStudioProjects\EderEngineTest\app\src\main\jniLibs

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

    echo [OK] %%A listo en %JNILIBS%\%%A\
)

echo.
echo [OK] Todos los ABIs compilados y copiados a jniLibs.
endlocal
PAUSE
