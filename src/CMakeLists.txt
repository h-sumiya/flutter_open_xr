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

set_target_properties(
  xr_quad
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin/MinSizeRel"
)

if(MSVC)
  target_compile_options(xr_quad PRIVATE /W4 /permissive-)
endif()
