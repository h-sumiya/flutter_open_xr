function(configure_app_target target_name)
  set_target_properties(
    ${target_name}
    PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
      RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
      RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
      RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/MinSizeRel"
  )

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
  endif()
endfunction()

add_executable(xr_quad xr_quad.cpp)

target_compile_definitions(
  xr_quad
  PRIVATE
    XR_USE_PLATFORM_WIN32
    XR_USE_GRAPHICS_API_D3D11
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(xr_quad PRIVATE OpenXR::openxr_loader d3d11 dxgi ole32)
configure_app_target(xr_quad)

find_program(FLUTTER_EXECUTABLE NAMES flutter)
if(NOT FLUTTER_EXECUTABLE)
  message(WARNING "flutter command was not found. Skipping flutter_offscreen target.")
  return()
endif()

# When flutter is resolved to a shim without extension, execute the .cmd wrapper.
if(
  WIN32
  AND NOT FLUTTER_EXECUTABLE MATCHES "\\.(exe|bat|cmd)$"
)
  if(EXISTS "${FLUTTER_EXECUTABLE}.cmd")
    set(FLUTTER_EXECUTABLE "${FLUTTER_EXECUTABLE}.cmd")
  elseif(EXISTS "${FLUTTER_EXECUTABLE}.bat")
    set(FLUTTER_EXECUTABLE "${FLUTTER_EXECUTABLE}.bat")
  endif()
endif()

execute_process(
  COMMAND "${FLUTTER_EXECUTABLE}" --version --machine
  RESULT_VARIABLE FLUTTER_VERSION_RESULT
  OUTPUT_VARIABLE FLUTTER_VERSION_JSON
  ERROR_VARIABLE FLUTTER_VERSION_STDERR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT FLUTTER_VERSION_RESULT EQUAL 0)
  message(
    WARNING
      "Failed to execute 'flutter --version --machine' using '${FLUTTER_EXECUTABLE}'. stderr: ${FLUTTER_VERSION_STDERR}"
  )
  return()
endif()

string(JSON FLUTTER_ROOT_RAW ERROR_VARIABLE FLUTTER_ROOT_JSON_ERROR GET "${FLUTTER_VERSION_JSON}" flutterRoot)
string(JSON FLUTTER_VERSION ERROR_VARIABLE FLUTTER_VERSION_JSON_ERROR GET "${FLUTTER_VERSION_JSON}" flutterVersion)
string(JSON FLUTTER_ENGINE_REVISION ERROR_VARIABLE FLUTTER_ENGINE_JSON_ERROR GET "${FLUTTER_VERSION_JSON}" engineRevision)

if(FLUTTER_ROOT_JSON_ERROR OR FLUTTER_VERSION_JSON_ERROR OR FLUTTER_ENGINE_JSON_ERROR)
  message(
    WARNING
      "Failed to parse Flutter version json. Skipping flutter_offscreen target."
  )
  return()
endif()

file(TO_CMAKE_PATH "${FLUTTER_ROOT_RAW}" FLUTTER_ROOT)
set(FLUTTER_SAMPLE_DIR "${CMAKE_SOURCE_DIR}/flutter_samples/offscreen_ui")
set(FLUTTER_GENERATED_DIR "${CMAKE_BINARY_DIR}/flutter")
set(FLUTTER_ASSET_DIR "${FLUTTER_GENERATED_DIR}/offscreen_ui_assets")
set(FLUTTER_EMBEDDER_URL
    "https://storage.googleapis.com/flutter_infra_release/flutter/${FLUTTER_ENGINE_REVISION}/windows-x64/windows-x64-embedder.zip"
)
set(FLUTTER_EMBEDDER_ZIP
    "${FLUTTER_GENERATED_DIR}/downloads/windows-x64-embedder-${FLUTTER_ENGINE_REVISION}.zip"
)
set(FLUTTER_EMBEDDER_DIR "${FLUTTER_GENERATED_DIR}/embedder/${FLUTTER_ENGINE_REVISION}")
set(FLUTTER_EMBEDDER_STAMP "${FLUTTER_GENERATED_DIR}/embedder_${FLUTTER_ENGINE_REVISION}.stamp")
set(FLUTTER_EMBEDDER_HEADER "${FLUTTER_EMBEDDER_DIR}/flutter_embedder.h")
set(FLUTTER_ENGINE_DLL "${FLUTTER_EMBEDDER_DIR}/flutter_engine.dll")
set(FLUTTER_ENGINE_IMPORT_LIB "${FLUTTER_EMBEDDER_DIR}/flutter_engine.dll.lib")
set(FLUTTER_ICUDTL_SOURCE "${FLUTTER_ROOT}/bin/cache/artifacts/engine/windows-x64/icudtl.dat")
set(FLUTTER_BUNDLE_STAMP "${FLUTTER_GENERATED_DIR}/bundle_${FLUTTER_ENGINE_REVISION}.stamp")
set(FLUTTER_PACKAGE_CONFIG "${FLUTTER_SAMPLE_DIR}/.dart_tool/package_config.json")

message(STATUS "Flutter SDK detected: ${FLUTTER_VERSION} (${FLUTTER_ENGINE_REVISION})")
message(STATUS "Flutter root: ${FLUTTER_ROOT}")
message(STATUS "Flutter embedder URL: ${FLUTTER_EMBEDDER_URL}")

add_custom_command(
  OUTPUT "${FLUTTER_EMBEDDER_STAMP}"
  BYPRODUCTS
    "${FLUTTER_EMBEDDER_HEADER}"
    "${FLUTTER_ENGINE_DLL}"
    "${FLUTTER_ENGINE_IMPORT_LIB}"
  COMMAND ${CMAKE_COMMAND} -DURL=${FLUTTER_EMBEDDER_URL} -DOUTPUT=${FLUTTER_EMBEDDER_ZIP}
          -P "${CMAKE_SOURCE_DIR}/cmake/DownloadFile.cmake"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${FLUTTER_EMBEDDER_DIR}"
  COMMAND ${CMAKE_COMMAND} -E chdir "${FLUTTER_EMBEDDER_DIR}" ${CMAKE_COMMAND} -E tar xvf
          "${FLUTTER_EMBEDDER_ZIP}" --format=zip
  COMMAND ${CMAKE_COMMAND} -E touch "${FLUTTER_EMBEDDER_STAMP}"
  COMMENT "Downloading Flutter embedder (${FLUTTER_ENGINE_REVISION})"
  VERBATIM
)
add_custom_target(flutter_step2_embedder DEPENDS "${FLUTTER_EMBEDDER_STAMP}")

add_custom_command(
  OUTPUT "${FLUTTER_PACKAGE_CONFIG}"
  COMMAND "${FLUTTER_EXECUTABLE}" pub get
  WORKING_DIRECTORY "${FLUTTER_SAMPLE_DIR}"
  DEPENDS "${FLUTTER_SAMPLE_DIR}/pubspec.yaml"
  COMMENT "Resolving Flutter sample package dependencies"
  VERBATIM
)

add_custom_command(
  OUTPUT "${FLUTTER_BUNDLE_STAMP}"
  BYPRODUCTS "${FLUTTER_ASSET_DIR}/kernel_blob.bin"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${FLUTTER_ASSET_DIR}"
  COMMAND
    "${FLUTTER_EXECUTABLE}" build bundle --debug --target-platform=windows-x64
    --asset-dir "${FLUTTER_ASSET_DIR}"
  COMMAND ${CMAKE_COMMAND} -E touch "${FLUTTER_BUNDLE_STAMP}"
  WORKING_DIRECTORY "${FLUTTER_SAMPLE_DIR}"
  DEPENDS
    "${FLUTTER_PACKAGE_CONFIG}"
    "${FLUTTER_SAMPLE_DIR}/lib/main.dart"
  COMMENT "Building Flutter offscreen sample assets"
  VERBATIM
)
add_custom_target(flutter_step2_assets DEPENDS "${FLUTTER_BUNDLE_STAMP}")

add_executable(flutter_offscreen flutter_offscreen.cpp)
target_include_directories(flutter_offscreen PRIVATE "${FLUTTER_EMBEDDER_DIR}")
target_compile_definitions(
  flutter_offscreen
  PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(flutter_offscreen PRIVATE "${FLUTTER_ENGINE_IMPORT_LIB}")
add_dependencies(flutter_offscreen flutter_step2_embedder flutter_step2_assets)
configure_app_target(flutter_offscreen)

add_custom_command(
  TARGET flutter_offscreen
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FLUTTER_ENGINE_DLL}"
          "$<TARGET_FILE_DIR:flutter_offscreen>/flutter_engine.dll"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:flutter_offscreen>/data"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${FLUTTER_ASSET_DIR}"
          "$<TARGET_FILE_DIR:flutter_offscreen>/data/flutter_assets"
  COMMENT "Copying Flutter runtime files next to flutter_offscreen"
)

if(EXISTS "${FLUTTER_ICUDTL_SOURCE}")
  add_custom_command(
    TARGET flutter_offscreen
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FLUTTER_ICUDTL_SOURCE}"
            "$<TARGET_FILE_DIR:flutter_offscreen>/icudtl.dat"
    COMMENT "Copying icudtl.dat from local Flutter SDK cache"
  )
else()
  message(
    WARNING
      "icudtl.dat was not found at ${FLUTTER_ICUDTL_SOURCE}. flutter_offscreen may fail at runtime."
  )
endif()
