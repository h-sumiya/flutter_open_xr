#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <conio.h>
#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr int32_t kSwapchainWidth = 1024;
constexpr int32_t kSwapchainHeight = 1024;
constexpr float kQuadWidthMeters = 1.2f;
constexpr float kQuadHeightMeters = 0.8f;
constexpr float kQuadDistanceMeters = 1.2f;

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

uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool bgraFormat) {
    if (bgraFormat) {
        return static_cast<uint32_t>(b) | (static_cast<uint32_t>(g) << 8U) | (static_cast<uint32_t>(r) << 16U) |
               (static_cast<uint32_t>(a) << 24U);
    }
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8U) | (static_cast<uint32_t>(b) << 16U) |
           (static_cast<uint32_t>(a) << 24U);
}

void FillCheckerboard(std::vector<uint32_t>& buffer, int32_t width, int32_t height, uint64_t frameIndex, bool bgraFormat) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid checkerboard size.");
    }

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    buffer.resize(pixelCount);

    constexpr int32_t kTileSize = 64;
    const uint32_t colorA = PackColor(245, 146, 26, 255, bgraFormat);
    const uint32_t colorB = PackColor(20, 20, 24, 255, bgraFormat);
    const uint32_t borderColor = PackColor(255, 255, 255, 255, bgraFormat);

    const int32_t animatedOffset = static_cast<int32_t>((frameIndex / 30) % 2);

    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const bool border = (x < 4) || (y < 4) || (x >= width - 4) || (y >= height - 4);
            if (border) {
                buffer[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = borderColor;
                continue;
            }

            const int32_t tx = x / kTileSize;
            const int32_t ty = y / kTileSize;
            const bool useA = ((tx + ty + animatedOffset) & 1) == 0;
            buffer[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = useA ? colorA : colorB;
        }
    }
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
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_B8G8R8A8_UNORM,
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

class QuadLayerApp {
   public:
    ~QuadLayerApp() {
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
        CreateQuadSwapchain();
    }

    void Run() {
        std::cout << "Quad sample started.\n";
        std::cout << "Press ESC or Q in this console to exit.\n";

        uint64_t frameIndex = 0;
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

            RenderFrame(frameIndex++);
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
        std::strncpy(createInfo.applicationInfo.applicationName, "flutter_xr_quad",
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
        swapchainCreateInfo.width = kSwapchainWidth;
        swapchainCreateInfo.height = kSwapchainHeight;
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

        checkerboard_.reserve(static_cast<size_t>(kSwapchainWidth) * static_cast<size_t>(kSwapchainHeight));
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
                sessionRunning_ = false;
                ThrowIfXrFailed(xrEndSession(session_), "xrEndSession", instance_);
                std::cout << "Session stopping.\n";
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                sessionRunning_ = false;
                exitRequested_ = true;
                break;
            default:
                break;
        }
    }

    void RenderFrame(uint64_t frameIndex) {
        XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        ThrowIfXrFailed(xrWaitFrame(session_, &frameWaitInfo, &frameState), "xrWaitFrame", instance_);

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

            FillCheckerboard(checkerboard_, kSwapchainWidth, kSwapchainHeight, frameIndex, isBgraFormat_);
            deviceContext_->UpdateSubresource(quadImages_[imageIndex].texture, 0, nullptr, checkerboard_.data(),
                                              kSwapchainWidth * static_cast<UINT>(sizeof(uint32_t)), 0);
            deviceContext_->Flush();

            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            ThrowIfXrFailed(xrReleaseSwapchainImage(quadSwapchain_, &releaseInfo), "xrReleaseSwapchainImage", instance_);

            quadLayer.space = appSpace_;
            quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quadLayer.subImage.swapchain = quadSwapchain_;
            quadLayer.subImage.imageRect.offset = {0, 0};
            quadLayer.subImage.imageRect.extent = {kSwapchainWidth, kSwapchainHeight};
            quadLayer.subImage.imageArrayIndex = 0;
            quadLayer.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
            quadLayer.pose.position = {0.0f, 0.0f, -kQuadDistanceMeters};
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
        if (quadSwapchain_ != XR_NULL_HANDLE) {
            xrDestroySwapchain(quadSwapchain_);
            quadSwapchain_ = XR_NULL_HANDLE;
        }

        if (appSpace_ != XR_NULL_HANDLE) {
            xrDestroySpace(appSpace_);
            appSpace_ = XR_NULL_HANDLE;
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
        quadImages_.clear();
        checkerboard_.clear();
    }

    XrInstance instance_{XR_NULL_HANDLE};
    XrSystemId systemId_{XR_NULL_SYSTEM_ID};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace appSpace_{XR_NULL_HANDLE};
    XrSwapchain quadSwapchain_{XR_NULL_HANDLE};

    XrViewConfigurationType viewConfigType_{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    XrEnvironmentBlendMode blendMode_{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    XrSessionState sessionState_{XR_SESSION_STATE_UNKNOWN};

    bool sessionRunning_{false};
    bool exitRequested_{false};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> deviceContext_;
    DXGI_FORMAT colorFormat_{DXGI_FORMAT_R8G8B8A8_UNORM};
    bool isBgraFormat_{false};

    std::vector<XrSwapchainImageD3D11KHR> quadImages_;
    std::vector<uint32_t> checkerboard_;
};

}  // namespace

int main() {
    try {
        ScopedComInitializer com;
        QuadLayerApp app;
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
