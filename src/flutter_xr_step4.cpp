#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <conio.h>
#include <objbase.h>
#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "flutter_embedder.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr int32_t kFlutterSurfaceWidth = 1280;
constexpr int32_t kFlutterSurfaceHeight = 720;
constexpr float kQuadWidthMeters = 1.2f;
constexpr float kQuadHeightMeters =
    kQuadWidthMeters * (static_cast<float>(kFlutterSurfaceHeight) / static_cast<float>(kFlutterSurfaceWidth));
constexpr float kQuadDistanceMeters = 1.2f;
constexpr DWORD kFirstFrameTimeoutMs = 15000;
constexpr float kTriggerPressThreshold = 0.75f;
constexpr float kTriggerReleaseThreshold = 0.65f;
constexpr int32_t kPointerDeviceId = 1;
constexpr int64_t kFlutterViewId = 0;

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

void ThrowIfXrFailed(XrResult result, const char* call, XrInstance instance = XR_NULL_HANDLE) {
    if (XR_FAILED(result)) {
        throw std::runtime_error(std::string(call) + " failed: " + XrResultToString(instance, result));
    }
}

bool LuidEquals(const LUID& lhs, const LUID& rhs) {
    return lhs.LowPart == rhs.LowPart && lhs.HighPart == rhs.HighPart;
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

bool Contains(const std::vector<XrViewConfigurationType>& values, XrViewConfigurationType target) {
    return std::find(values.begin(), values.end(), target) != values.end();
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

    const std::array<XrEnvironmentBlendMode, 3> preferred = {XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
                                                              XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,
                                                              XR_ENVIRONMENT_BLEND_MODE_ADDITIVE};

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

class ScopedComInitializer {
   public:
    ScopedComInitializer() {
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

    ~ScopedComInitializer() {
        if (initialized_) {
            CoUninitialize();
        }
    }

   private:
    bool initialized_ = false;
};

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

XrVector3f RotateVector(const XrQuaternionf& rotation, const XrVector3f& value) {
    const XrVector3f qv{rotation.x, rotation.y, rotation.z};
    const XrVector3f term1 = Scale(qv, 2.0f * Dot(qv, value));
    const XrVector3f term2 = Scale(value, rotation.w * rotation.w - Dot(qv, qv));
    const XrVector3f term3 = Scale(Cross(qv, value), 2.0f * rotation.w);
    return Add(Add(term1, term2), term3);
}

bool IntersectRayWithQuad(const XrVector3f& rayOriginWorld,
                          const XrVector3f& rayDirectionWorld,
                          const XrPosef& quadPoseWorld,
                          float quadWidthMeters,
                          float quadHeightMeters,
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

struct FlutterFrame {
    std::mutex mutex;
    std::vector<uint8_t> pixels;
    size_t rowBytes = 0;
    size_t width = 0;
    size_t height = 0;
    uint64_t frameIndex = 0;
};

struct FlutterBridgeState {
    FlutterFrame latestFrame;
    HANDLE firstFrameEvent = nullptr;
};

struct PointerHitResult {
    bool onQuad = false;
    double xPixels = static_cast<double>(kFlutterSurfaceWidth) * 0.5;
    double yPixels = static_cast<double>(kFlutterSurfaceHeight) * 0.5;
};

XrPosef MakeQuadPose() {
    XrPosef pose{};
    pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    pose.position = {0.0f, 0.0f, -kQuadDistanceMeters};
    return pose;
}

bool OnSurfacePresent(void* user_data, const void* allocation, size_t row_bytes, size_t height) {
    if (user_data == nullptr || allocation == nullptr || row_bytes < 4 || height == 0) {
        return false;
    }

    auto* bridge = static_cast<FlutterBridgeState*>(user_data);
    const size_t frameBytes = row_bytes * height;

    {
        std::lock_guard<std::mutex> lock(bridge->latestFrame.mutex);
        bridge->latestFrame.pixels.resize(frameBytes);
        std::memcpy(bridge->latestFrame.pixels.data(), allocation, frameBytes);
        bridge->latestFrame.rowBytes = row_bytes;
        bridge->latestFrame.width = row_bytes / 4;
        bridge->latestFrame.height = height;
        bridge->latestFrame.frameIndex += 1;
    }

    if (bridge->firstFrameEvent != nullptr) {
        SetEvent(bridge->firstFrameEvent);
    }
    return true;
}

class FlutterXrStep4App {
   public:
    ~FlutterXrStep4App() {
        try {
            Shutdown();
        } catch (...) {
            // Best-effort cleanup.
        }
    }

    void Initialize() {
        CreateInstance();
        InitializeSystem();
        InitializeD3D11Device();
        CreateSession();
        CreateReferenceSpace();
        InitializeInputActions();
        CreateQuadSwapchain();
        CreateFlutterTexture();
        InitializeFlutterEngine();
    }

    void Run() {
        std::cout << "Step4 input integration sample started.\n";
        std::cout << "Press ESC or Q in this console to exit.\n";

        while (!exitRequested_) {
            PollEvents();
            if (exitRequested_) {
                break;
            }

            if (_kbhit()) {
                const int c = _getch();
                if (c == 27 || c == 'q' || c == 'Q') {
                    exitRequested_ = true;
                    break;
                }
            }

            if (!sessionRunning_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            RenderFrame();
        }
    }

   private:
    void CreateInstance() {
        const std::array<const char*, 1> requiredExtensions = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};

        uint32_t extensionCount = 0;
        ThrowIfXrFailed(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr),
                        "xrEnumerateInstanceExtensionProperties(count)");

        std::vector<XrExtensionProperties> extensionProps(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
        ThrowIfXrFailed(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensionProps.data()),
                        "xrEnumerateInstanceExtensionProperties(data)");

        for (const char* required : requiredExtensions) {
            const bool found =
                std::any_of(extensionProps.begin(), extensionProps.end(),
                            [&](const XrExtensionProperties& prop) { return std::strcmp(prop.extensionName, required) == 0; });
            if (!found) {
                throw std::runtime_error(std::string("Required extension not available: ") + required);
            }
        }

        XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        std::strncpy(createInfo.applicationInfo.applicationName, "flutter_xr_step4",
                     sizeof(createInfo.applicationInfo.applicationName) - 1);
        createInfo.applicationInfo.applicationVersion = 1;
        std::strncpy(createInfo.applicationInfo.engineName, "custom", sizeof(createInfo.applicationInfo.engineName) - 1);
        createInfo.applicationInfo.engineVersion = 1;
        createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
        createInfo.enabledExtensionNames = requiredExtensions.data();

        ThrowIfXrFailed(xrCreateInstance(&createInfo, &instance_), "xrCreateInstance");

        XrInstanceProperties instanceProps{XR_TYPE_INSTANCE_PROPERTIES};
        ThrowIfXrFailed(xrGetInstanceProperties(instance_, &instanceProps), "xrGetInstanceProperties", instance_);
        std::cout << "OpenXR runtime: " << instanceProps.runtimeName << "\n";
    }

    void InitializeSystem() {
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        ThrowIfXrFailed(xrGetSystem(instance_, &systemInfo, &systemId_), "xrGetSystem", instance_);

        viewConfigType_ = SelectViewConfigurationType(instance_, systemId_);
        blendMode_ = SelectBlendMode(instance_, systemId_, viewConfigType_);
    }

    void InitializeD3D11Device() {
        PFN_xrGetD3D11GraphicsRequirementsKHR getGraphicsRequirements = nullptr;
        ThrowIfXrFailed(
            xrGetInstanceProcAddr(instance_, "xrGetD3D11GraphicsRequirementsKHR",
                                  reinterpret_cast<PFN_xrVoidFunction*>(&getGraphicsRequirements)),
            "xrGetInstanceProcAddr(xrGetD3D11GraphicsRequirementsKHR)", instance_);

        XrGraphicsRequirementsD3D11KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        ThrowIfXrFailed(getGraphicsRequirements(instance_, systemId_, &graphicsRequirements), "xrGetD3D11GraphicsRequirementsKHR",
                        instance_);

        ComPtr<IDXGIAdapter1> adapter = FindAdapterByLuid(graphicsRequirements.adapterLuid);

        std::vector<D3D_FEATURE_LEVEL> featureLevels = {
            D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        };
        featureLevels.erase(
            std::remove_if(featureLevels.begin(), featureLevels.end(),
                           [&](D3D_FEATURE_LEVEL level) { return level < graphicsRequirements.minFeatureLevel; }),
            featureLevels.end());

        if (featureLevels.empty()) {
            throw std::runtime_error("Runtime requested an unsupported minimum D3D feature level.");
        }

        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if !defined(NDEBUG)
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL createdLevel{};
        HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, creationFlags, featureLevels.data(),
                                       static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                                       device_.ReleaseAndGetAddressOf(), &createdLevel, deviceContext_.ReleaseAndGetAddressOf());
        if (FAILED(hr) && hr == DXGI_ERROR_SDK_COMPONENT_MISSING && (creationFlags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
            creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, creationFlags, featureLevels.data(),
                                   static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION,
                                   device_.ReleaseAndGetAddressOf(), &createdLevel, deviceContext_.ReleaseAndGetAddressOf());
        }
        ThrowIfFailed(hr, "D3D11CreateDevice");
    }

    void CreateSession() {
        XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        graphicsBinding.device = device_.Get();

        XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionCreateInfo.next = &graphicsBinding;
        sessionCreateInfo.systemId = systemId_;

        ThrowIfXrFailed(xrCreateSession(instance_, &sessionCreateInfo, &session_), "xrCreateSession", instance_);
    }

    void CreateReferenceSpace() {
        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
        spaceInfo.poseInReferenceSpace.orientation.x = 0.0f;
        spaceInfo.poseInReferenceSpace.orientation.y = 0.0f;
        spaceInfo.poseInReferenceSpace.orientation.z = 0.0f;
        spaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

        ThrowIfXrFailed(xrCreateReferenceSpace(session_, &spaceInfo, &appSpace_), "xrCreateReferenceSpace", instance_);
    }

    void SuggestBindings(XrPath interactionProfile, const std::vector<XrActionSuggestedBinding>& bindings) {
        XrInteractionProfileSuggestedBinding suggestedBindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestedBindings.interactionProfile = interactionProfile;
        suggestedBindings.suggestedBindings = bindings.data();
        suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        ThrowIfXrFailed(xrSuggestInteractionProfileBindings(instance_, &suggestedBindings),
                        "xrSuggestInteractionProfileBindings", instance_);
    }

    void InitializeInputActions() {
        XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strcpy(actionSetInfo.actionSetName, "flutter_input");
        std::strcpy(actionSetInfo.localizedActionSetName, "Flutter Input");
        actionSetInfo.priority = 0;
        ThrowIfXrFailed(xrCreateActionSet(instance_, &actionSetInfo, &inputActionSet_), "xrCreateActionSet", instance_);

        ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right", &rightHandPath_), "xrStringToPath(/user/hand/right)",
                        instance_);

        {
            XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
            actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
            std::strcpy(actionInfo.actionName, "pointer_pose");
            std::strcpy(actionInfo.localizedActionName, "Pointer Pose");
            actionInfo.countSubactionPaths = 1;
            actionInfo.subactionPaths = &rightHandPath_;
            ThrowIfXrFailed(xrCreateAction(inputActionSet_, &actionInfo, &pointerPoseAction_), "xrCreateAction(pointerPose)",
                            instance_);
        }

        {
            XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
            actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
            std::strcpy(actionInfo.actionName, "trigger_value");
            std::strcpy(actionInfo.localizedActionName, "Trigger Value");
            actionInfo.countSubactionPaths = 1;
            actionInfo.subactionPaths = &rightHandPath_;
            ThrowIfXrFailed(xrCreateAction(inputActionSet_, &actionInfo, &triggerValueAction_),
                            "xrCreateAction(triggerValue)", instance_);
        }

        XrPath rightSelectClickPath = XR_NULL_PATH;
        XrPath rightTriggerValuePath = XR_NULL_PATH;
        XrPath rightAimPosePath = XR_NULL_PATH;
        XrPath rightGripPosePath = XR_NULL_PATH;

        ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/select/click", &rightSelectClickPath),
                        "xrStringToPath(right select click)", instance_);
        ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/trigger/value", &rightTriggerValuePath),
                        "xrStringToPath(right trigger value)", instance_);
        ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/aim/pose", &rightAimPosePath),
                        "xrStringToPath(right aim pose)", instance_);
        ThrowIfXrFailed(xrStringToPath(instance_, "/user/hand/right/input/grip/pose", &rightGripPosePath),
                        "xrStringToPath(right grip pose)", instance_);

        XrPath khrSimpleInteractionProfile = XR_NULL_PATH;
        XrPath oculusTouchInteractionProfile = XR_NULL_PATH;
        XrPath viveInteractionProfile = XR_NULL_PATH;
        XrPath indexInteractionProfile = XR_NULL_PATH;
        XrPath microsoftMotionInteractionProfile = XR_NULL_PATH;

        ThrowIfXrFailed(xrStringToPath(instance_, "/interaction_profiles/khr/simple_controller", &khrSimpleInteractionProfile),
                        "xrStringToPath(khr simple profile)", instance_);
        ThrowIfXrFailed(
            xrStringToPath(instance_, "/interaction_profiles/oculus/touch_controller", &oculusTouchInteractionProfile),
            "xrStringToPath(oculus touch profile)", instance_);
        ThrowIfXrFailed(xrStringToPath(instance_, "/interaction_profiles/htc/vive_controller", &viveInteractionProfile),
                        "xrStringToPath(vive profile)", instance_);
        ThrowIfXrFailed(xrStringToPath(instance_, "/interaction_profiles/valve/index_controller", &indexInteractionProfile),
                        "xrStringToPath(index profile)", instance_);
        ThrowIfXrFailed(
            xrStringToPath(instance_, "/interaction_profiles/microsoft/motion_controller", &microsoftMotionInteractionProfile),
            "xrStringToPath(microsoft motion profile)", instance_);

        SuggestBindings(khrSimpleInteractionProfile,
                        {{triggerValueAction_, rightSelectClickPath}, {pointerPoseAction_, rightGripPosePath}});
        SuggestBindings(oculusTouchInteractionProfile,
                        {{triggerValueAction_, rightTriggerValuePath}, {pointerPoseAction_, rightAimPosePath}});
        SuggestBindings(viveInteractionProfile,
                        {{triggerValueAction_, rightTriggerValuePath}, {pointerPoseAction_, rightGripPosePath}});
        SuggestBindings(indexInteractionProfile,
                        {{triggerValueAction_, rightTriggerValuePath}, {pointerPoseAction_, rightGripPosePath}});
        SuggestBindings(microsoftMotionInteractionProfile,
                        {{triggerValueAction_, rightTriggerValuePath}, {pointerPoseAction_, rightGripPosePath}});

        XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attachInfo.countActionSets = 1;
        attachInfo.actionSets = &inputActionSet_;
        ThrowIfXrFailed(xrAttachSessionActionSets(session_, &attachInfo), "xrAttachSessionActionSets", instance_);

        XrActionSpaceCreateInfo actionSpaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        actionSpaceInfo.action = pointerPoseAction_;
        actionSpaceInfo.subactionPath = rightHandPath_;
        actionSpaceInfo.poseInActionSpace.orientation.w = 1.0f;
        ThrowIfXrFailed(xrCreateActionSpace(session_, &actionSpaceInfo, &pointerSpace_), "xrCreateActionSpace(pointer)",
                        instance_);
    }

    PointerHitResult QueryPointerHit(XrTime predictedDisplayTime) {
        PointerHitResult result;
        if (pointerSpace_ == XR_NULL_HANDLE) {
            return result;
        }

        XrActionStateGetInfo poseGetInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        poseGetInfo.action = pointerPoseAction_;
        poseGetInfo.subactionPath = rightHandPath_;
        XrActionStatePose poseState{XR_TYPE_ACTION_STATE_POSE};
        ThrowIfXrFailed(xrGetActionStatePose(session_, &poseGetInfo, &poseState), "xrGetActionStatePose(pointerPose)",
                        instance_);
        if (poseState.isActive != XR_TRUE) {
            return result;
        }

        XrSpaceLocation pointerLocation{XR_TYPE_SPACE_LOCATION};
        const XrResult locateResult = xrLocateSpace(pointerSpace_, appSpace_, predictedDisplayTime, &pointerLocation);
        if (XR_FAILED(locateResult)) {
            return result;
        }

        constexpr XrSpaceLocationFlags kRequiredFlags =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ((pointerLocation.locationFlags & kRequiredFlags) != kRequiredFlags) {
            return result;
        }

        const XrVector3f rayForward = RotateVector(pointerLocation.pose.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
        const XrPosef quadPose = MakeQuadPose();

        double u = 0.0;
        double v = 0.0;
        if (!IntersectRayWithQuad(pointerLocation.pose.position, rayForward, quadPose, kQuadWidthMeters, kQuadHeightMeters,
                                  &u, &v)) {
            return result;
        }

        result.onQuad = true;
        result.xPixels =
            std::clamp(u * static_cast<double>(kFlutterSurfaceWidth), 0.0, static_cast<double>(kFlutterSurfaceWidth - 1));
        result.yPixels =
            std::clamp(v * static_cast<double>(kFlutterSurfaceHeight), 0.0, static_cast<double>(kFlutterSurfaceHeight - 1));
        return result;
    }

    bool SendFlutterPointerEvent(FlutterPointerPhase phase, double xPixels, double yPixels, int64_t buttons) {
        if (flutterEngine_ == nullptr) {
            return false;
        }

        FlutterPointerEvent event{};
        event.struct_size = sizeof(event);
        event.phase = phase;
        event.timestamp = static_cast<size_t>(FlutterEngineGetCurrentTime());
        event.x = xPixels;
        event.y = yPixels;
        event.device = kPointerDeviceId;
        event.signal_kind = kFlutterPointerSignalKindNone;
        event.device_kind = kFlutterPointerDeviceKindMouse;
        event.buttons = buttons;
        event.view_id = kFlutterViewId;

        const FlutterEngineResult result = FlutterEngineSendPointerEvent(flutterEngine_, &event, 1);
        if (result != kSuccess) {
            std::cerr << "[warn] FlutterEngineSendPointerEvent failed. phase=" << static_cast<int32_t>(phase)
                      << " result=" << static_cast<int32_t>(result) << "\n";
            return false;
        }

        lastPointerX_ = xPixels;
        lastPointerY_ = yPixels;
        return true;
    }

    void EnsureFlutterPointerAdded(double xPixels, double yPixels) {
        if (pointerAdded_) {
            return;
        }
        if (SendFlutterPointerEvent(kAdd, xPixels, yPixels, 0)) {
            pointerAdded_ = true;
        }
    }

    void PollInput(XrTime predictedDisplayTime) {
        if (inputActionSet_ == XR_NULL_HANDLE || flutterEngine_ == nullptr) {
            return;
        }

        if (sessionState_ != XR_SESSION_STATE_FOCUSED) {
            if (pointerDown_) {
                SendFlutterPointerEvent(kUp, lastPointerX_, lastPointerY_, 0);
                pointerDown_ = false;
            }
            triggerPressed_ = false;
            return;
        }

        const XrActiveActionSet activeActionSet{inputActionSet_, XR_NULL_PATH};
        XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &activeActionSet;
        ThrowIfXrFailed(xrSyncActions(session_, &syncInfo), "xrSyncActions", instance_);

        const PointerHitResult hit = QueryPointerHit(predictedDisplayTime);

        XrActionStateGetInfo triggerGetInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        triggerGetInfo.action = triggerValueAction_;
        triggerGetInfo.subactionPath = rightHandPath_;
        XrActionStateFloat triggerState{XR_TYPE_ACTION_STATE_FLOAT};
        ThrowIfXrFailed(xrGetActionStateFloat(session_, &triggerGetInfo, &triggerState), "xrGetActionStateFloat(trigger)",
                        instance_);

        const bool inputActive = (triggerState.isActive == XR_TRUE);
        const float triggerValue = inputActive ? triggerState.currentState : 0.0f;
        const bool pressedNow =
            triggerPressed_ ? (triggerValue >= kTriggerReleaseThreshold) : (triggerValue >= kTriggerPressThreshold);

        if (pressedNow && !triggerPressed_) {
            if (hit.onQuad) {
                EnsureFlutterPointerAdded(hit.xPixels, hit.yPixels);
                if (pointerAdded_ &&
                    SendFlutterPointerEvent(kDown, hit.xPixels, hit.yPixels, kFlutterPointerButtonMousePrimary)) {
                    pointerDown_ = true;
                }
            }
        } else if ((!pressedNow || !inputActive) && triggerPressed_) {
            if (pointerDown_) {
                const double upX = hit.onQuad ? hit.xPixels : lastPointerX_;
                const double upY = hit.onQuad ? hit.yPixels : lastPointerY_;
                SendFlutterPointerEvent(kUp, upX, upY, 0);
                pointerDown_ = false;
            }
        }

        triggerPressed_ = inputActive && pressedNow;
    }

    void CreateQuadSwapchain() {
        uint32_t formatCount = 0;
        ThrowIfXrFailed(xrEnumerateSwapchainFormats(session_, 0, &formatCount, nullptr), "xrEnumerateSwapchainFormats(count)",
                        instance_);
        if (formatCount == 0) {
            throw std::runtime_error("Runtime returned zero swapchain formats.");
        }

        std::vector<int64_t> formats(formatCount);
        ThrowIfXrFailed(xrEnumerateSwapchainFormats(session_, formatCount, &formatCount, formats.data()),
                        "xrEnumerateSwapchainFormats(data)", instance_);

        colorFormat_ = SelectSwapchainFormat(formats);
        isBgraFormat_ = IsBgraFormat(colorFormat_);

        XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapchainCreateInfo.createFlags = 0;
        swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainCreateInfo.format = static_cast<int64_t>(colorFormat_);
        swapchainCreateInfo.sampleCount = 1;
        swapchainCreateInfo.width = kFlutterSurfaceWidth;
        swapchainCreateInfo.height = kFlutterSurfaceHeight;
        swapchainCreateInfo.faceCount = 1;
        swapchainCreateInfo.arraySize = 1;
        swapchainCreateInfo.mipCount = 1;

        ThrowIfXrFailed(xrCreateSwapchain(session_, &swapchainCreateInfo, &quadSwapchain_), "xrCreateSwapchain", instance_);

        uint32_t imageCount = 0;
        ThrowIfXrFailed(xrEnumerateSwapchainImages(quadSwapchain_, 0, &imageCount, nullptr), "xrEnumerateSwapchainImages(count)",
                        instance_);
        if (imageCount == 0) {
            throw std::runtime_error("Runtime returned zero swapchain images.");
        }

        quadImages_.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        ThrowIfXrFailed(
            xrEnumerateSwapchainImages(
                quadSwapchain_, imageCount, &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(quadImages_.data())),
            "xrEnumerateSwapchainImages(data)", instance_);
    }

    void CreateFlutterTexture() {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(kFlutterSurfaceWidth);
        desc.Height = static_cast<UINT>(kFlutterSurfaceHeight);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = colorFormat_;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        ThrowIfFailed(device_->CreateTexture2D(&desc, nullptr, flutterTexture_.ReleaseAndGetAddressOf()),
                      "ID3D11Device::CreateTexture2D(flutterTexture)");

        const std::vector<uint32_t> initialPixels(
            static_cast<size_t>(kFlutterSurfaceWidth) * static_cast<size_t>(kFlutterSurfaceHeight), 0xFF101010u);
        deviceContext_->UpdateSubresource(flutterTexture_.Get(), 0, nullptr, initialPixels.data(),
                                          static_cast<UINT>(kFlutterSurfaceWidth * sizeof(uint32_t)), 0);
    }

    void InitializeFlutterEngine() {
        const auto exeDir = GetExecutableDir();
        const auto assetsDir = exeDir / "data" / "flutter_assets";
        const auto kernelBlob = assetsDir / "kernel_blob.bin";
        const auto icuPath = exeDir / "icudtl.dat";

        if (!std::filesystem::exists(kernelBlob)) {
            throw std::runtime_error("Missing Flutter assets: " + kernelBlob.string());
        }

        assetsPathUtf8_ = WideToUtf8(assetsDir.wstring());
        if (std::filesystem::exists(icuPath)) {
            icuPathUtf8_ = WideToUtf8(icuPath.wstring());
        } else {
            std::cout << "[warn] icudtl.dat not found next to executable. Trying without explicit ICU path.\n";
        }

        flutterBridge_.firstFrameEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (flutterBridge_.firstFrameEvent == nullptr) {
            throw std::runtime_error("CreateEventW(firstFrameEvent) failed.");
        }

        FlutterRendererConfig rendererConfig{};
        rendererConfig.type = kSoftware;
        rendererConfig.software.struct_size = sizeof(FlutterSoftwareRendererConfig);
        rendererConfig.software.surface_present_callback = OnSurfacePresent;

        const char* commandLineArgs[] = {"flutter_xr_step4", "--enable-impeller=false"};
        FlutterProjectArgs projectArgs{};
        projectArgs.struct_size = sizeof(FlutterProjectArgs);
        projectArgs.assets_path = assetsPathUtf8_.c_str();
        projectArgs.icu_data_path = icuPathUtf8_.empty() ? nullptr : icuPathUtf8_.c_str();
        projectArgs.command_line_argc = static_cast<int>(std::size(commandLineArgs));
        projectArgs.command_line_argv = commandLineArgs;

        const FlutterEngineResult runResult =
            FlutterEngineRun(FLUTTER_ENGINE_VERSION, &rendererConfig, &projectArgs, &flutterBridge_, &flutterEngine_);
        if (runResult != kSuccess || flutterEngine_ == nullptr) {
            throw std::runtime_error("FlutterEngineRun failed. result=" + std::to_string(static_cast<int32_t>(runResult)));
        }

        FlutterWindowMetricsEvent metrics{};
        metrics.struct_size = sizeof(metrics);
        metrics.width = kFlutterSurfaceWidth;
        metrics.height = kFlutterSurfaceHeight;
        metrics.pixel_ratio = 1.0;
        metrics.view_id = kFlutterViewId;
        const FlutterEngineResult metricsResult = FlutterEngineSendWindowMetricsEvent(flutterEngine_, &metrics);
        if (metricsResult != kSuccess) {
            throw std::runtime_error("FlutterEngineSendWindowMetricsEvent failed. result=" +
                                     std::to_string(static_cast<int32_t>(metricsResult)));
        }

        std::cout << "Waiting for first Flutter frame (timeout " << kFirstFrameTimeoutMs << " ms)...\n";
        const DWORD waitResult = WaitForSingleObject(flutterBridge_.firstFrameEvent, kFirstFrameTimeoutMs);
        if (waitResult == WAIT_OBJECT_0) {
            std::lock_guard<std::mutex> lock(flutterBridge_.latestFrame.mutex);
            std::cout << "Flutter first frame received: " << flutterBridge_.latestFrame.width << "x"
                      << flutterBridge_.latestFrame.height << " frameIndex=" << flutterBridge_.latestFrame.frameIndex << "\n";
        } else if (waitResult == WAIT_TIMEOUT) {
            std::cout << "[warn] Timed out waiting for the first Flutter frame. Continuing.\n";
        } else {
            throw std::runtime_error("WaitForSingleObject(firstFrameEvent) failed.");
        }
    }

    bool UploadLatestFlutterFrame() {
        FlutterFrame snapshot;
        {
            std::lock_guard<std::mutex> lock(flutterBridge_.latestFrame.mutex);
            if (flutterBridge_.latestFrame.frameIndex == 0 || flutterBridge_.latestFrame.frameIndex == uploadedFrameIndex_) {
                return false;
            }
            snapshot.rowBytes = flutterBridge_.latestFrame.rowBytes;
            snapshot.width = flutterBridge_.latestFrame.width;
            snapshot.height = flutterBridge_.latestFrame.height;
            snapshot.frameIndex = flutterBridge_.latestFrame.frameIndex;
            snapshot.pixels = flutterBridge_.latestFrame.pixels;
        }

        if (snapshot.width == 0 || snapshot.height == 0 || snapshot.rowBytes < snapshot.width * 4 || snapshot.pixels.empty()) {
            return false;
        }

        const size_t uploadWidth = std::min(snapshot.width, static_cast<size_t>(kFlutterSurfaceWidth));
        const size_t uploadHeight = std::min(snapshot.height, static_cast<size_t>(kFlutterSurfaceHeight));
        if (uploadWidth == 0 || uploadHeight == 0) {
            return false;
        }

        const uint8_t* uploadPixels = snapshot.pixels.data();
        size_t uploadRowBytes = snapshot.rowBytes;
        if (isBgraFormat_) {
            if (!ConvertRgbaToBgra(snapshot.pixels.data(), snapshot.rowBytes, uploadWidth, uploadHeight, convertedPixels_)) {
                return false;
            }
            uploadPixels = convertedPixels_.data();
            uploadRowBytes = uploadWidth * 4;
        }

        D3D11_BOX dstBox{};
        dstBox.left = 0;
        dstBox.top = 0;
        dstBox.front = 0;
        dstBox.right = static_cast<UINT>(uploadWidth);
        dstBox.bottom = static_cast<UINT>(uploadHeight);
        dstBox.back = 1;

        deviceContext_->UpdateSubresource(flutterTexture_.Get(), 0, &dstBox, uploadPixels, static_cast<UINT>(uploadRowBytes), 0);
        uploadedFrameIndex_ = snapshot.frameIndex;
        return true;
    }

    void PollEvents() {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult pollResult = xrPollEvent(instance_, &event);
        while (pollResult == XR_SUCCESS) {
            switch (event.type) {
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                    std::cerr << "OpenXR instance loss pending. Exiting.\n";
                    exitRequested_ = true;
                    break;
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                    HandleSessionStateChanged(*changed);
                    break;
                }
                default:
                    break;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
            pollResult = xrPollEvent(instance_, &event);
        }

        if (pollResult != XR_EVENT_UNAVAILABLE) {
            ThrowIfXrFailed(pollResult, "xrPollEvent", instance_);
        }
    }

    void HandleSessionStateChanged(const XrEventDataSessionStateChanged& changed) {
        sessionState_ = changed.state;

        switch (sessionState_) {
            case XR_SESSION_STATE_READY: {
                XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                beginInfo.primaryViewConfigurationType = viewConfigType_;
                ThrowIfXrFailed(xrBeginSession(session_, &beginInfo), "xrBeginSession", instance_);
                sessionRunning_ = true;
                std::cout << "Session started.\n";
                break;
            }
            case XR_SESSION_STATE_STOPPING:
                if (pointerDown_) {
                    SendFlutterPointerEvent(kUp, lastPointerX_, lastPointerY_, 0);
                    pointerDown_ = false;
                }
                triggerPressed_ = false;
                sessionRunning_ = false;
                ThrowIfXrFailed(xrEndSession(session_), "xrEndSession", instance_);
                std::cout << "Session stopping.\n";
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                if (pointerDown_) {
                    SendFlutterPointerEvent(kUp, lastPointerX_, lastPointerY_, 0);
                    pointerDown_ = false;
                }
                triggerPressed_ = false;
                sessionRunning_ = false;
                exitRequested_ = true;
                break;
            default:
                break;
        }
    }

    void RenderFrame() {
        XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        ThrowIfXrFailed(xrWaitFrame(session_, &frameWaitInfo, &frameState), "xrWaitFrame", instance_);

        PollInput(frameState.predictedDisplayTime);

        XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        ThrowIfXrFailed(xrBeginFrame(session_, &frameBeginInfo), "xrBeginFrame", instance_);

        XrCompositionLayerQuad quadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
        std::array<XrCompositionLayerBaseHeader*, 1> layers{};
        uint32_t layerCount = 0;

        if (frameState.shouldRender == XR_TRUE) {
            uint32_t imageIndex = 0;
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            ThrowIfXrFailed(xrAcquireSwapchainImage(quadSwapchain_, &acquireInfo, &imageIndex), "xrAcquireSwapchainImage",
                            instance_);

            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            ThrowIfXrFailed(xrWaitSwapchainImage(quadSwapchain_, &waitInfo), "xrWaitSwapchainImage", instance_);

            UploadLatestFlutterFrame();
            deviceContext_->CopyResource(quadImages_[imageIndex].texture, flutterTexture_.Get());
            deviceContext_->Flush();

            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            ThrowIfXrFailed(xrReleaseSwapchainImage(quadSwapchain_, &releaseInfo), "xrReleaseSwapchainImage", instance_);

            quadLayer.space = appSpace_;
            quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quadLayer.subImage.swapchain = quadSwapchain_;
            quadLayer.subImage.imageRect.offset = {0, 0};
            quadLayer.subImage.imageRect.extent = {kFlutterSurfaceWidth, kFlutterSurfaceHeight};
            quadLayer.subImage.imageArrayIndex = 0;
            quadLayer.pose = MakeQuadPose();
            quadLayer.size = {kQuadWidthMeters, kQuadHeightMeters};

            layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&quadLayer);
            layerCount = 1;
        }

        XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
        frameEndInfo.displayTime = frameState.predictedDisplayTime;
        frameEndInfo.environmentBlendMode = blendMode_;
        frameEndInfo.layerCount = layerCount;
        frameEndInfo.layers = (layerCount > 0) ? layers.data() : nullptr;
        ThrowIfXrFailed(xrEndFrame(session_, &frameEndInfo), "xrEndFrame", instance_);
    }

    void Shutdown() {
        if (flutterEngine_ != nullptr && pointerAdded_) {
            SendFlutterPointerEvent(kRemove, lastPointerX_, lastPointerY_, 0);
            pointerAdded_ = false;
        }

        if (flutterEngine_ != nullptr) {
            const FlutterEngineResult shutdownResult = FlutterEngineShutdown(flutterEngine_);
            if (shutdownResult != kSuccess) {
                std::cerr << "[warn] FlutterEngineShutdown failed. result="
                          << static_cast<int32_t>(shutdownResult) << "\n";
            }
            flutterEngine_ = nullptr;
        }

        if (flutterBridge_.firstFrameEvent != nullptr) {
            CloseHandle(flutterBridge_.firstFrameEvent);
            flutterBridge_.firstFrameEvent = nullptr;
        }

        if (quadSwapchain_ != XR_NULL_HANDLE) {
            xrDestroySwapchain(quadSwapchain_);
            quadSwapchain_ = XR_NULL_HANDLE;
        }

        if (pointerSpace_ != XR_NULL_HANDLE) {
            xrDestroySpace(pointerSpace_);
            pointerSpace_ = XR_NULL_HANDLE;
        }

        if (appSpace_ != XR_NULL_HANDLE) {
            xrDestroySpace(appSpace_);
            appSpace_ = XR_NULL_HANDLE;
        }

        if (triggerValueAction_ != XR_NULL_HANDLE) {
            xrDestroyAction(triggerValueAction_);
            triggerValueAction_ = XR_NULL_HANDLE;
        }

        if (pointerPoseAction_ != XR_NULL_HANDLE) {
            xrDestroyAction(pointerPoseAction_);
            pointerPoseAction_ = XR_NULL_HANDLE;
        }

        if (inputActionSet_ != XR_NULL_HANDLE) {
            xrDestroyActionSet(inputActionSet_);
            inputActionSet_ = XR_NULL_HANDLE;
        }

        if (session_ != XR_NULL_HANDLE) {
            if (sessionRunning_) {
                xrEndSession(session_);
                sessionRunning_ = false;
            }
            xrDestroySession(session_);
            session_ = XR_NULL_HANDLE;
        }

        if (instance_ != XR_NULL_HANDLE) {
            xrDestroyInstance(instance_);
            instance_ = XR_NULL_HANDLE;
        }

        deviceContext_.Reset();
        device_.Reset();
        flutterTexture_.Reset();
        quadImages_.clear();
        convertedPixels_.clear();
    }

    XrInstance instance_{XR_NULL_HANDLE};
    XrSystemId systemId_{XR_NULL_SYSTEM_ID};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace appSpace_{XR_NULL_HANDLE};
    XrSpace pointerSpace_{XR_NULL_HANDLE};
    XrSwapchain quadSwapchain_{XR_NULL_HANDLE};
    XrActionSet inputActionSet_{XR_NULL_HANDLE};
    XrAction pointerPoseAction_{XR_NULL_HANDLE};
    XrAction triggerValueAction_{XR_NULL_HANDLE};
    XrPath rightHandPath_{XR_NULL_PATH};

    XrViewConfigurationType viewConfigType_{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    XrEnvironmentBlendMode blendMode_{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    XrSessionState sessionState_{XR_SESSION_STATE_UNKNOWN};

    bool sessionRunning_{false};
    bool exitRequested_{false};
    bool triggerPressed_{false};
    bool pointerAdded_{false};
    bool pointerDown_{false};
    double lastPointerX_{static_cast<double>(kFlutterSurfaceWidth) * 0.5};
    double lastPointerY_{static_cast<double>(kFlutterSurfaceHeight) * 0.5};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> deviceContext_;
    DXGI_FORMAT colorFormat_{DXGI_FORMAT_R8G8B8A8_UNORM};
    bool isBgraFormat_{false};

    std::vector<XrSwapchainImageD3D11KHR> quadImages_;
    ComPtr<ID3D11Texture2D> flutterTexture_;
    FlutterEngine flutterEngine_{nullptr};
    FlutterBridgeState flutterBridge_;
    uint64_t uploadedFrameIndex_{0};
    std::vector<uint8_t> convertedPixels_;
    std::string assetsPathUtf8_;
    std::string icuPathUtf8_;
};

}  // namespace

int main() {
    try {
        ScopedComInitializer com;
        FlutterXrStep4App app;
        app.Initialize();
        app.Run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[fatal] " << ex.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "[fatal] Unknown exception\n";
        return 1;
    }
}
