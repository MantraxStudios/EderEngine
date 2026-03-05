@echo off
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=2& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGraphics/vendor/lib/SDL3.dll C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/SDL3.dll || (set FAIL_LINE=3& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=4& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/EderGame.exe C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/EderGame.exe || (set FAIL_LINE=5& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=6& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/EderPlayer.exe C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/EderPlayer.exe || (set FAIL_LINE=7& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=8& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/EderGraphics.dll C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/EderGraphics.dll || (set FAIL_LINE=9& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=10& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/lua54.dll C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/lua54.dll || (set FAIL_LINE=11& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=12& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/PhysX_64.dll C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/PhysX_64.dll || (set FAIL_LINE=13& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=14& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/PhysXCommon_64.dll C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/PhysXCommon_64.dll || (set FAIL_LINE=15& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=16& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/PhysXFoundation_64.dll C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/PhysXFoundation_64.dll || (set FAIL_LINE=17& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=18& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_if_different C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/PhysXCooking_64.dll C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/PhysXCooking_64.dll || (set FAIL_LINE=19& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=20& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_directory C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/Game/Content/shaders C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/Game/Content/shaders || (set FAIL_LINE=21& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=22& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E copy_directory C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/../EderGame/build/Game/Content/meshes C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/Game/Content/meshes || (set FAIL_LINE=23& goto :ABORT)
cd /D C:\Users\tupap\OneDrive\Escritorio\EderEngine\EderHub\build || (set FAIL_LINE=24& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/Game/Content/scenes || (set FAIL_LINE=25& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/Game/Content/scripts || (set FAIL_LINE=26& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/Game/Content/materials || (set FAIL_LINE=27& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/Game/Content/textures || (set FAIL_LINE=28& goto :ABORT)
"C:\Program Files\CMake\bin\cmake.exe" -E make_directory C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderHub/build/Game/Content/meshes || (set FAIL_LINE=29& goto :ABORT)
goto :EOF

:ABORT
set ERROR_CODE=%ERRORLEVEL%
echo Batch file failed at line %FAIL_LINE% with errorcode %ERRORLEVEL%
exit /b %ERROR_CODE%