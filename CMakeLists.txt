cmake_minimum_required(VERSION 3.21)
project(flutter_xr LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This project currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OPENXR_SDK_DIR "${CMAKE_SOURCE_DIR}/reference/OpenXR-SDK")
if(NOT EXISTS "${OPENXR_SDK_DIR}/CMakeLists.txt")
  message(
    FATAL_ERROR
      "Missing ${OPENXR_SDK_DIR}. Please clone OpenXR-SDK into reference/OpenXR-SDK."
  )
endif()

# Keep OpenXR subproject minimal.
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SDK_TESTS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)

add_subdirectory("${OPENXR_SDK_DIR}" "${CMAKE_BINARY_DIR}/openxr" EXCLUDE_FROM_ALL)
add_subdirectory(src)
