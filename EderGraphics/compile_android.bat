@echo off
setlocal

set NDK=C:\Users\tupap\AppData\Local\Android\Sdk\ndk\29.0.14206865
set ABI=armeabi-v7a
set PLATFORM=android-21
set BUILD_DIR=build-android
set BUILD_TYPE=Release

echo [INFO] Compilando EderGraphics para Android (%ABI%, %PLATFORM%)
echo.

cmake -S . -B %BUILD_DIR% ^
    -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE="%NDK%\build\cmake\android.toolchain.cmake" ^
    -DANDROID_ABI=%ABI% ^
    -DANDROID_PLATFORM=%PLATFORM% ^
    -DANDROID_STL=c++_shared ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

if %ERRORLEVEL% neq 0 (
    echo [FAILED] cmake configure
    exit /b 1
)

cmake --build %BUILD_DIR%

if %ERRORLEVEL% neq 0 (
    echo [FAILED] cmake build
    exit /b 1
)

echo.
echo [OK] EderGraphics.so generado en %BUILD_DIR%\
endlocal
