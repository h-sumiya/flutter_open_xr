#include "flutter_xr/shared.h"

#include <objbase.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace flutter_xr {

namespace {

bool LuidEquals(const LUID& lhs, const LUID& rhs) {
    return lhs.LowPart == rhs.LowPart && lhs.HighPart == rhs.HighPart;
}

bool Contains(const std::vector<XrViewConfigurationType>& values, XrViewConfigurationType target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

}  // namespace

std::string HResultToString(HRESULT hr) {
    std::ostringstream oss;
    oss << "HRESULT=0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    return oss.str();
}

void ThrowIfFailed(HRESULT hr, const char* call) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(call) + " failed: " + HResultToString(hr));
    }
}

std::string XrResultToString(XrInstance instance, XrResult result) {
    if (instance != XR_NULL_HANDLE) {
        char buffer[XR_MAX_RESULT_STRING_SIZE];
        if (XR_SUCCEEDED(xrResultToString(instance, result, buffer))) {
            return std::string(buffer);
        }
    }
    return std::to_string(static_cast<int32_t>(result));
}

void ThrowIfXrFailed(XrResult result, const char* call, XrInstance instance) {
    if (XR_FAILED(result)) {
        throw std::runtime_error(std::string(call) + " failed: " + XrResultToString(instance, result));
    }
}

ComPtr<IDXGIAdapter1> FindAdapterByLuid(const LUID& luid) {
    ComPtr<IDXGIFactory1> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf())), "CreateDXGIFactory1");

    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hr = factory->EnumAdapters1(i, adapter.GetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        ThrowIfFailed(hr, "IDXGIFactory1::EnumAdapters1");

        DXGI_ADAPTER_DESC1 desc{};
        ThrowIfFailed(adapter->GetDesc1(&desc), "IDXGIAdapter1::GetDesc1");
        if (LuidEquals(desc.AdapterLuid, luid)) {
            return adapter;
        }
    }

    throw std::runtime_error("No DXGI adapter matched the OpenXR runtime LUID.");
}

bool IsBgraFormat(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

XrViewConfigurationType SelectViewConfigurationType(XrInstance instance, XrSystemId systemId) {
    uint32_t viewConfigCount = 0;
    ThrowIfXrFailed(xrEnumerateViewConfigurations(instance, systemId, 0, &viewConfigCount, nullptr),
                    "xrEnumerateViewConfigurations(count)", instance);
    if (viewConfigCount == 0) {
        throw std::runtime_error("Runtime reported no view configuration types.");
    }

    std::vector<XrViewConfigurationType> viewConfigs(viewConfigCount);
    ThrowIfXrFailed(xrEnumerateViewConfigurations(instance, systemId, viewConfigCount, &viewConfigCount, viewConfigs.data()),
                    "xrEnumerateViewConfigurations(data)", instance);

    if (Contains(viewConfigs, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)) {
        return XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    }
    if (Contains(viewConfigs, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO)) {
        return XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO;
    }
    return viewConfigs.front();
}

XrEnvironmentBlendMode SelectBlendMode(XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigType) {
    uint32_t blendModeCount = 0;
    ThrowIfXrFailed(xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigType, 0, &blendModeCount, nullptr),
                    "xrEnumerateEnvironmentBlendModes(count)", instance);
    if (blendModeCount == 0) {
        throw std::runtime_error("Runtime reported no environment blend modes.");
    }

    std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
    ThrowIfXrFailed(xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigType, blendModeCount, &blendModeCount,
                                                     blendModes.data()),
                    "xrEnumerateEnvironmentBlendModes(data)", instance);

    const std::array<XrEnvironmentBlendMode, 3> preferred = {
        XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
        XR_ENVIRONMENT_BLEND_MODE_ADDITIVE,
    };
    for (XrEnvironmentBlendMode candidate : preferred) {
        if (std::find(blendModes.begin(), blendModes.end(), candidate) != blendModes.end()) {
            return candidate;
        }
    }
    return blendModes.front();
}

DXGI_FORMAT SelectSwapchainFormat(const std::vector<int64_t>& runtimeFormats) {
    const std::array<DXGI_FORMAT, 4> preferredFormats = {
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
    };

    for (DXGI_FORMAT preferred : preferredFormats) {
        if (std::find(runtimeFormats.begin(), runtimeFormats.end(), static_cast<int64_t>(preferred)) != runtimeFormats.end()) {
            return preferred;
        }
    }

    throw std::runtime_error("Runtime does not expose a supported RGBA/BGRA color swapchain format.");
}

ScopedComInitializer::ScopedComInitializer() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        initialized_ = true;
        return;
    }
    if (hr == RPC_E_CHANGED_MODE) {
        return;
    }
    ThrowIfFailed(hr, "CoInitializeEx");
}

ScopedComInitializer::~ScopedComInitializer() {
    if (initialized_) {
        CoUninitialize();
    }
}

std::filesystem::path GetExecutableDir() {
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD copied = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (copied == 0 || copied == MAX_PATH) {
        return std::filesystem::current_path();
    }
    buffer.resize(copied);
    return std::filesystem::path(buffer).parent_path();
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return std::string();
    }

    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), needed - 1, nullptr, nullptr);
    return out;
}

XrVector3f Add(const XrVector3f& lhs, const XrVector3f& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

XrVector3f Subtract(const XrVector3f& lhs, const XrVector3f& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

XrVector3f Scale(const XrVector3f& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float Dot(const XrVector3f& lhs, const XrVector3f& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

XrVector3f Cross(const XrVector3f& lhs, const XrVector3f& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

XrQuaternionf Conjugate(const XrQuaternionf& value) {
    return {-value.x, -value.y, -value.z, value.w};
}

XrQuaternionf Multiply(const XrQuaternionf& lhs, const XrQuaternionf& rhs) {
    return {
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

XrVector3f RotateVector(const XrQuaternionf& rotation, const XrVector3f& value) {
    const XrVector3f qv{rotation.x, rotation.y, rotation.z};
    const XrVector3f term1 = Scale(qv, 2.0f * Dot(qv, value));
    const XrVector3f term2 = Scale(value, rotation.w * rotation.w - Dot(qv, qv));
    const XrVector3f term3 = Scale(Cross(qv, value), 2.0f * rotation.w);
    return Add(Add(term1, term2), term3);
}

XrVector3f Normalize(const XrVector3f& value) {
    const float lengthSquared = Dot(value, value);
    if (lengthSquared <= 1.0e-8f) {
        return {0.0f, 0.0f, -1.0f};
    }
    const float invLength = 1.0f / std::sqrt(lengthSquared);
    return Scale(value, invLength);
}

bool IntersectRayWithQuad(const XrVector3f& rayOriginWorld,
                          const XrVector3f& rayDirectionWorld,
                          const XrPosef& quadPoseWorld,
                          float quadWidthMeters,
                          float quadHeightMeters,
                          float* outHitDistanceMeters,
                          double* outU,
                          double* outV) {
    if (outU == nullptr || outV == nullptr || quadWidthMeters <= 0.0f || quadHeightMeters <= 0.0f) {
        return false;
    }

    const XrQuaternionf invQuadOrientation = Conjugate(quadPoseWorld.orientation);
    const XrVector3f rayOriginLocal = RotateVector(invQuadOrientation, Subtract(rayOriginWorld, quadPoseWorld.position));
    const XrVector3f rayDirectionLocal = RotateVector(invQuadOrientation, rayDirectionWorld);

    if (std::abs(rayDirectionLocal.z) < 1.0e-6f) {
        return false;
    }

    const float t = -rayOriginLocal.z / rayDirectionLocal.z;
    if (t <= 0.0f) {
        return false;
    }

    const XrVector3f hit = Add(rayOriginLocal, Scale(rayDirectionLocal, t));
    const float halfWidth = quadWidthMeters * 0.5f;
    const float halfHeight = quadHeightMeters * 0.5f;
    if (std::abs(hit.x) > halfWidth || std::abs(hit.y) > halfHeight) {
        return false;
    }

    *outU = static_cast<double>(hit.x / quadWidthMeters + 0.5f);
    *outV = static_cast<double>(0.5f - hit.y / quadHeightMeters);
    if (outHitDistanceMeters != nullptr) {
        *outHitDistanceMeters = t;
    }
    return true;
}

bool ConvertRgbaToBgra(const uint8_t* source,
                       size_t sourceRowBytes,
                       size_t width,
                       size_t height,
                       std::vector<uint8_t>& outPixels) {
    if (source == nullptr || width == 0 || height == 0 || sourceRowBytes < width * 4) {
        return false;
    }

    outPixels.resize(width * height * 4);
    for (size_t y = 0; y < height; ++y) {
        const uint8_t* src = source + y * sourceRowBytes;
        uint8_t* dst = outPixels.data() + y * width * 4;
        for (size_t x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    return true;
}

XrPosef MakeQuadPose() {
    XrPosef pose{};
    pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    pose.position = {0.0f, 0.0f, -kQuadDistanceMeters};
    return pose;
}

}  // namespace flutter_xr
