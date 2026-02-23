#pragma once

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

namespace flutter_xr {

using Microsoft::WRL::ComPtr;

inline constexpr int32_t kFlutterSurfaceWidth = 1280;
inline constexpr int32_t kFlutterSurfaceHeight = 720;
inline constexpr float kQuadWidthMeters = 1.2f;
inline constexpr float kQuadHeightMeters =
    kQuadWidthMeters * (static_cast<float>(kFlutterSurfaceHeight) / static_cast<float>(kFlutterSurfaceWidth));
inline constexpr float kQuadDistanceMeters = 1.2f;
inline constexpr int32_t kPointerRayTextureWidth = 256;
inline constexpr int32_t kPointerRayTextureHeight = 8;
inline constexpr float kPointerRayThicknessMeters = 0.01f;
inline constexpr float kPointerRayFallbackLengthMeters = 2.0f;
inline constexpr float kPointerRayMinLengthMeters = 0.05f;
inline constexpr DWORD kFirstFrameTimeoutMs = 15000;
inline constexpr float kTriggerPressThreshold = 0.75f;
inline constexpr float kTriggerReleaseThreshold = 0.65f;
inline constexpr int32_t kPointerDeviceId = 1;
inline constexpr int64_t kFlutterViewId = 0;

std::string HResultToString(HRESULT hr);
void ThrowIfFailed(HRESULT hr, const char* call);

std::string XrResultToString(XrInstance instance, XrResult result);
void ThrowIfXrFailed(XrResult result, const char* call, XrInstance instance = XR_NULL_HANDLE);

ComPtr<IDXGIAdapter1> FindAdapterByLuid(const LUID& luid);

bool IsBgraFormat(DXGI_FORMAT format);
XrViewConfigurationType SelectViewConfigurationType(XrInstance instance, XrSystemId systemId);
XrEnvironmentBlendMode SelectBlendMode(XrInstance instance,
                                       XrSystemId systemId,
                                       XrViewConfigurationType viewConfigType);
DXGI_FORMAT SelectSwapchainFormat(const std::vector<int64_t>& runtimeFormats);

class ScopedComInitializer {
   public:
    ScopedComInitializer();
    ~ScopedComInitializer();

   private:
    bool initialized_ = false;
};

std::filesystem::path GetExecutableDir();
std::string WideToUtf8(const std::wstring& wide);

XrVector3f Add(const XrVector3f& lhs, const XrVector3f& rhs);
XrVector3f Subtract(const XrVector3f& lhs, const XrVector3f& rhs);
XrVector3f Scale(const XrVector3f& value, float scale);
float Dot(const XrVector3f& lhs, const XrVector3f& rhs);
XrVector3f Cross(const XrVector3f& lhs, const XrVector3f& rhs);
XrQuaternionf Conjugate(const XrQuaternionf& value);
XrQuaternionf Multiply(const XrQuaternionf& lhs, const XrQuaternionf& rhs);
XrVector3f RotateVector(const XrQuaternionf& rotation, const XrVector3f& value);
XrVector3f Normalize(const XrVector3f& value);

bool IntersectRayWithQuad(const XrVector3f& rayOriginWorld,
                          const XrVector3f& rayDirectionWorld,
                          const XrPosef& quadPoseWorld,
                          float quadWidthMeters,
                          float quadHeightMeters,
                          float* outHitDistanceMeters,
                          double* outU,
                          double* outV);

bool ConvertRgbaToBgra(const uint8_t* source,
                       size_t sourceRowBytes,
                       size_t width,
                       size_t height,
                       std::vector<uint8_t>& outPixels);

XrPosef MakeQuadPose();

}  // namespace flutter_xr
