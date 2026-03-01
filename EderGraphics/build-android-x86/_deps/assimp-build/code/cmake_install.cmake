# Install script for directory: C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/EderGraphics")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "0")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "C:/Users/tupap/AppData/Local/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-objdump.exe")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libassimp5.3.0" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libassimp.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libassimp.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libassimp.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-build/bin/libassimp.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libassimp.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libassimp.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "C:/Users/tupap/AppData/Local/Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libassimp.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libassimp5.3.0" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "assimp-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/assimp" TYPE FILE FILES
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/anim.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/aabb.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/ai_assert.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/camera.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/color4.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/color4.inl"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-build/code/../include/assimp/config.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/ColladaMetaData.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/commonMetaData.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/defs.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/cfileio.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/light.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/material.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/material.inl"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/matrix3x3.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/matrix3x3.inl"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/matrix4x4.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/matrix4x4.inl"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/mesh.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/ObjMaterial.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/pbrmaterial.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/GltfMaterial.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/postprocess.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/quaternion.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/quaternion.inl"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/scene.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/metadata.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/texture.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/types.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/vector2.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/vector2.inl"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/vector3.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/vector3.inl"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/version.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/cimport.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/AssertHandler.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/importerdesc.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Importer.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/DefaultLogger.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/ProgressHandler.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/IOStream.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/IOSystem.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Logger.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/LogStream.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/NullLogger.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/cexport.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Exporter.hpp"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/DefaultIOStream.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/DefaultIOSystem.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/ZipArchiveIOSystem.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/SceneCombiner.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/fast_atof.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/qnan.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/BaseImporter.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Hash.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/MemoryIOWrapper.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/ParsingUtils.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/StreamReader.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/StreamWriter.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/StringComparison.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/StringUtils.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/SGSpatialSort.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/GenericProperty.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/SpatialSort.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/SkeletonMeshBuilder.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/SmallVector.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/SmoothingGroups.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/SmoothingGroups.inl"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/StandardShapes.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/RemoveComments.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Subdivision.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Vertex.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/LineSplitter.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/TinyFormatter.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Profiler.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/LogAux.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Bitmap.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/XMLTools.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/IOStreamBuffer.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/CreateAnimMesh.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/XmlParser.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/BlobIOSystem.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/MathFunctions.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Exceptional.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/ByteSwapper.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Base64.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "assimp-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/assimp/Compiler" TYPE FILE FILES
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Compiler/pushpack1.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Compiler/poppack1.h"
    "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-src/code/../include/assimp/Compiler/pstdint.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/tupap/OneDrive/Escritorio/EderEngine/EderGraphics/build-android-x86/_deps/assimp-build/code/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
