cmake_minimum_required(VERSION 3.21)
project(flutter_open_xr_native LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "flutter_open_xr native runner currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED OPENXR_SDK_DIR OR OPENXR_SDK_DIR STREQUAL "")
  message(FATAL_ERROR "OPENXR_SDK_DIR is required.")
endif()
if(NOT EXISTS "${OPENXR_SDK_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "OPENXR_SDK_DIR is invalid: ${OPENXR_SDK_DIR}")
endif()

if(NOT DEFINED FLUTTER_EMBEDDER_DIR OR FLUTTER_EMBEDDER_DIR STREQUAL "")
  message(FATAL_ERROR "FLUTTER_EMBEDDER_DIR is required.")
endif()
if(NOT EXISTS "${FLUTTER_EMBEDDER_DIR}/flutter_embedder.h")
  message(FATAL_ERROR "flutter_embedder.h was not found in FLUTTER_EMBEDDER_DIR: ${FLUTTER_EMBEDDER_DIR}")
endif()
if(NOT EXISTS "${FLUTTER_EMBEDDER_DIR}/flutter_engine.dll")
  message(FATAL_ERROR "flutter_engine.dll was not found in FLUTTER_EMBEDDER_DIR: ${FLUTTER_EMBEDDER_DIR}")
endif()
if(NOT EXISTS "${FLUTTER_EMBEDDER_DIR}/flutter_engine.dll.lib")
  message(FATAL_ERROR "flutter_engine.dll.lib was not found in FLUTTER_EMBEDDER_DIR: ${FLUTTER_EMBEDDER_DIR}")
endif()

if(NOT DEFINED FLUTTER_ASSETS_DIR OR FLUTTER_ASSETS_DIR STREQUAL "")
  message(FATAL_ERROR "FLUTTER_ASSETS_DIR is required.")
endif()
if(NOT EXISTS "${FLUTTER_ASSETS_DIR}/kernel_blob.bin")
  message(FATAL_ERROR "kernel_blob.bin was not found in FLUTTER_ASSETS_DIR: ${FLUTTER_ASSETS_DIR}")
endif()

set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)

add_subdirectory("${OPENXR_SDK_DIR}" "${CMAKE_BINARY_DIR}/openxr" EXCLUDE_FROM_ALL)

set(FLUTTER_ENGINE_IMPORT_LIB "${FLUTTER_EMBEDDER_DIR}/flutter_engine.dll.lib")
set(FLUTTER_ENGINE_DLL "${FLUTTER_EMBEDDER_DIR}/flutter_engine.dll")

add_library(
  flutter_open_xr_runtime
  STATIC
    src/flutter_xr/shared.cpp
    src/flutter_xr/app_core.cpp
    src/flutter_xr/app_input.cpp
    src/flutter_xr/app_flutter.cpp
)

target_include_directories(
  flutter_open_xr_runtime
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${FLUTTER_EMBEDDER_DIR}"
)

target_compile_definitions(
  flutter_open_xr_runtime
  PUBLIC
    XR_USE_PLATFORM_WIN32
    XR_USE_GRAPHICS_API_D3D11
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(
  flutter_open_xr_runtime
  PUBLIC
    OpenXR::openxr_loader
    d3d11
    dxgi
    ole32
    "${FLUTTER_ENGINE_IMPORT_LIB}"
)

if(MSVC)
  target_compile_options(flutter_open_xr_runtime PRIVATE /W4 /permissive-)
endif()

add_executable(flutter_open_xr_runner src/flutter_xr/main.cpp)
target_link_libraries(flutter_open_xr_runner PRIVATE flutter_open_xr_runtime)

set_target_properties(
  flutter_open_xr_runner
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/MinSizeRel"
)

add_custom_command(
  TARGET flutter_open_xr_runner
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FLUTTER_ENGINE_DLL}"
          "$<TARGET_FILE_DIR:flutter_open_xr_runner>/flutter_engine.dll"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:flutter_open_xr_runner>/data"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${FLUTTER_ASSETS_DIR}"
          "$<TARGET_FILE_DIR:flutter_open_xr_runner>/data/flutter_assets"
  COMMENT "Copying Flutter runtime files next to flutter_open_xr_runner"
)

if(DEFINED FLUTTER_ICUDTL_PATH AND NOT FLUTTER_ICUDTL_PATH STREQUAL "" AND EXISTS "${FLUTTER_ICUDTL_PATH}")
  add_custom_command(
    TARGET flutter_open_xr_runner
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FLUTTER_ICUDTL_PATH}"
            "$<TARGET_FILE_DIR:flutter_open_xr_runner>/icudtl.dat"
    COMMENT "Copying icudtl.dat"
  )
endif()
