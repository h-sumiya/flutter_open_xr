#include "flutter_xr/app.h"

#include <conio.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace flutter_xr {

namespace {

uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool bgraFormat) {
    if (bgraFormat) {
        return static_cast<uint32_t>(b) | (static_cast<uint32_t>(g) << 8U) | (static_cast<uint32_t>(r) << 16U) |
               (static_cast<uint32_t>(a) << 24U);
    }
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8U) | (static_cast<uint32_t>(b) << 16U) |
           (static_cast<uint32_t>(a) << 24U);
}

}  // namespace

FlutterXrApp::~FlutterXrApp() {
    try {
        Shutdown();
    } catch (...) {
        // Best-effort cleanup.
    }
}

void FlutterXrApp::Initialize() {
    CreateInstance();
    InitializeSystem();
    InitializeD3D11Device();
    CreateSession();
    CreateReferenceSpace();
    pointerRayPose_.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    pointerRayPose_.position = {0.0f, 0.0f, 0.0f};
    pointerRayLengthMeters_ = kPointerRayFallbackLengthMeters;
    leftPointerRayPose_.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    leftPointerRayPose_.position = {0.0f, 0.0f, 0.0f};
    leftPointerRayLengthMeters_ = kPointerRayFallbackLengthMeters;
    InitializeInputActions();
    CreateQuadSwapchain();
    CreateBackgroundSwapchain();
    CreatePointerRaySwapchain();
    CreateFlutterTexture();
    CreateBackgroundTexture();
    CreatePointerRayTexture();
    InitializeFlutterEngine();
}

void FlutterXrApp::Run() {
    std::cout << "Flutter XR sample started.\n";
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

void FlutterXrApp::CreateInstance() {
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
    std::strncpy(createInfo.applicationInfo.applicationName, "flutter_open_xr",
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

void FlutterXrApp::InitializeSystem() {
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    ThrowIfXrFailed(xrGetSystem(instance_, &systemInfo, &systemId_), "xrGetSystem", instance_);

    viewConfigType_ = SelectViewConfigurationType(instance_, systemId_);
    blendMode_ = SelectBlendMode(instance_, systemId_, viewConfigType_);
}

void FlutterXrApp::InitializeD3D11Device() {
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
    featureLevels.erase(std::remove_if(featureLevels.begin(), featureLevels.end(),
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

void FlutterXrApp::CreateSession() {
    XrGraphicsBindingD3D11KHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    graphicsBinding.device = device_.Get();

    XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.next = &graphicsBinding;
    sessionCreateInfo.systemId = systemId_;

    ThrowIfXrFailed(xrCreateSession(instance_, &sessionCreateInfo, &session_), "xrCreateSession", instance_);
}

void FlutterXrApp::CreateReferenceSpace() {
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    spaceInfo.poseInReferenceSpace.orientation.x = 0.0f;
    spaceInfo.poseInReferenceSpace.orientation.y = 0.0f;
    spaceInfo.poseInReferenceSpace.orientation.z = 0.0f;
    spaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};

    ThrowIfXrFailed(xrCreateReferenceSpace(session_, &spaceInfo, &appSpace_), "xrCreateReferenceSpace", instance_);
}

void FlutterXrApp::CreateQuadSwapchain() {
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
    ThrowIfXrFailed(xrEnumerateSwapchainImages(quadSwapchain_, imageCount, &imageCount,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(quadImages_.data())),
                    "xrEnumerateSwapchainImages(data)", instance_);
}

void FlutterXrApp::CreatePointerRaySwapchain() {
    XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainCreateInfo.createFlags = 0;
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.format = static_cast<int64_t>(colorFormat_);
    swapchainCreateInfo.sampleCount = 1;
    swapchainCreateInfo.width = kPointerRayTextureWidth;
    swapchainCreateInfo.height = kPointerRayTextureHeight;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 1;
    swapchainCreateInfo.mipCount = 1;

    ThrowIfXrFailed(xrCreateSwapchain(session_, &swapchainCreateInfo, &pointerRaySwapchain_), "xrCreateSwapchain(pointerRay)",
                    instance_);

    uint32_t imageCount = 0;
    ThrowIfXrFailed(xrEnumerateSwapchainImages(pointerRaySwapchain_, 0, &imageCount, nullptr),
                    "xrEnumerateSwapchainImages(pointerRay count)", instance_);
    if (imageCount == 0) {
        throw std::runtime_error("Runtime returned zero pointer-ray swapchain images.");
    }

    pointerRayImages_.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    ThrowIfXrFailed(
        xrEnumerateSwapchainImages(pointerRaySwapchain_, imageCount, &imageCount,
                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(pointerRayImages_.data())),
        "xrEnumerateSwapchainImages(pointerRay data)", instance_);
}

void FlutterXrApp::CreateFlutterTexture() {
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

void FlutterXrApp::CreatePointerRayTexture() {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(kPointerRayTextureWidth);
    desc.Height = static_cast<UINT>(kPointerRayTextureHeight);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = colorFormat_;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    ThrowIfFailed(device_->CreateTexture2D(&desc, nullptr, pointerRayTexture_.ReleaseAndGetAddressOf()),
                  "ID3D11Device::CreateTexture2D(pointerRayTexture)");

    const uint32_t rayColor = PackColor(100, 220, 255, 230, isBgraFormat_);
    const std::vector<uint32_t> pixels(static_cast<size_t>(kPointerRayTextureWidth) * static_cast<size_t>(kPointerRayTextureHeight),
                                       rayColor);
    deviceContext_->UpdateSubresource(pointerRayTexture_.Get(), 0, nullptr, pixels.data(),
                                      static_cast<UINT>(kPointerRayTextureWidth * sizeof(uint32_t)), 0);
}

void FlutterXrApp::PollEvents() {
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

void FlutterXrApp::HandleSessionStateChanged(const XrEventDataSessionStateChanged& changed) {
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
            pointerRayVisible_ = false;
            leftPointerRayVisible_ = false;
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
            pointerRayVisible_ = false;
            leftPointerRayVisible_ = false;
            sessionRunning_ = false;
            exitRequested_ = true;
            break;
        default:
            break;
    }
}

void FlutterXrApp::RenderFrame() {
    XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    ThrowIfXrFailed(xrWaitFrame(session_, &frameWaitInfo, &frameState), "xrWaitFrame", instance_);

    PollInput(frameState.predictedDisplayTime);

    XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    ThrowIfXrFailed(xrBeginFrame(session_, &frameBeginInfo), "xrBeginFrame", instance_);

    XrCompositionLayerQuad backgroundLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad quadLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad, 2> pointerRayLayers = {
        XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD},
        XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD},
    };
    std::array<XrCompositionLayerBaseHeader*, 4> layers{};
    uint32_t layerCount = 0;

    if (frameState.shouldRender == XR_TRUE) {
        if (IsBackgroundEnabled() && backgroundSwapchain_ != XR_NULL_HANDLE && backgroundTexture_ != nullptr) {
            uint32_t imageIndex = 0;
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            ThrowIfXrFailed(xrAcquireSwapchainImage(backgroundSwapchain_, &acquireInfo, &imageIndex),
                            "xrAcquireSwapchainImage(background)", instance_);

            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            ThrowIfXrFailed(xrWaitSwapchainImage(backgroundSwapchain_, &waitInfo), "xrWaitSwapchainImage(background)",
                            instance_);

            UploadBackgroundTexture();
            deviceContext_->CopyResource(backgroundImages_[imageIndex].texture, backgroundTexture_.Get());

            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            ThrowIfXrFailed(xrReleaseSwapchainImage(backgroundSwapchain_, &releaseInfo), "xrReleaseSwapchainImage(background)",
                            instance_);

            backgroundLayer.space = appSpace_;
            backgroundLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            backgroundLayer.subImage.swapchain = backgroundSwapchain_;
            backgroundLayer.subImage.imageRect.offset = {0, 0};
            backgroundLayer.subImage.imageRect.extent = {kBackgroundTextureWidth, kBackgroundTextureHeight};
            backgroundLayer.subImage.imageArrayIndex = 0;
            backgroundLayer.pose = MakeGroundPose();
            backgroundLayer.size = {kGroundQuadWidthMeters, kGroundQuadDepthMeters};

            layers[layerCount++] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&backgroundLayer);
        }

        {
            uint32_t imageIndex = 0;
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            ThrowIfXrFailed(xrAcquireSwapchainImage(quadSwapchain_, &acquireInfo, &imageIndex), "xrAcquireSwapchainImage",
                            instance_);

            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            ThrowIfXrFailed(xrWaitSwapchainImage(quadSwapchain_, &waitInfo), "xrWaitSwapchainImage", instance_);

            UploadLatestFlutterFrame();
            deviceContext_->CopyResource(quadImages_[imageIndex].texture, flutterTexture_.Get());

            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            ThrowIfXrFailed(xrReleaseSwapchainImage(quadSwapchain_, &releaseInfo), "xrReleaseSwapchainImage", instance_);
        }

        quadLayer.space = appSpace_;
        quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quadLayer.subImage.swapchain = quadSwapchain_;
        quadLayer.subImage.imageRect.offset = {0, 0};
        quadLayer.subImage.imageRect.extent = {kFlutterSurfaceWidth, kFlutterSurfaceHeight};
        quadLayer.subImage.imageArrayIndex = 0;
        quadLayer.pose = MakeQuadPose();
        quadLayer.size = {kQuadWidthMeters, kQuadHeightMeters};

        layers[layerCount++] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&quadLayer);

        const bool hasAnyPointerRay =
            (pointerRayVisible_ || leftPointerRayVisible_) && pointerRaySwapchain_ != XR_NULL_HANDLE &&
            pointerRayTexture_ != nullptr;
        if (hasAnyPointerRay) {
            uint32_t rayImageIndex = 0;
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            ThrowIfXrFailed(xrAcquireSwapchainImage(pointerRaySwapchain_, &acquireInfo, &rayImageIndex),
                            "xrAcquireSwapchainImage(pointerRay)", instance_);

            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            ThrowIfXrFailed(xrWaitSwapchainImage(pointerRaySwapchain_, &waitInfo), "xrWaitSwapchainImage(pointerRay)",
                            instance_);

            deviceContext_->CopyResource(pointerRayImages_[rayImageIndex].texture, pointerRayTexture_.Get());

            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            ThrowIfXrFailed(xrReleaseSwapchainImage(pointerRaySwapchain_, &releaseInfo), "xrReleaseSwapchainImage(pointerRay)",
                            instance_);

            uint32_t pointerRayLayerCount = 0;
            auto appendPointerRayLayer = [&](const XrPosef& pose, float lengthMeters) {
                XrCompositionLayerQuad& pointerRayLayer = pointerRayLayers[pointerRayLayerCount++];
                pointerRayLayer.space = appSpace_;
                pointerRayLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                pointerRayLayer.subImage.swapchain = pointerRaySwapchain_;
                pointerRayLayer.subImage.imageRect.offset = {0, 0};
                pointerRayLayer.subImage.imageRect.extent = {kPointerRayTextureWidth, kPointerRayTextureHeight};
                pointerRayLayer.subImage.imageArrayIndex = 0;
                pointerRayLayer.pose = pose;
                pointerRayLayer.size = {lengthMeters, kPointerRayThicknessMeters};
                layers[layerCount++] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&pointerRayLayer);
            };

            if (pointerRayVisible_) {
                appendPointerRayLayer(pointerRayPose_, pointerRayLengthMeters_);
            }
            if (leftPointerRayVisible_) {
                appendPointerRayLayer(leftPointerRayPose_, leftPointerRayLengthMeters_);
            }
        }

        deviceContext_->Flush();
    }

    XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
    frameEndInfo.displayTime = frameState.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = blendMode_;
    frameEndInfo.layerCount = layerCount;
    frameEndInfo.layers = (layerCount > 0) ? layers.data() : nullptr;
    ThrowIfXrFailed(xrEndFrame(session_, &frameEndInfo), "xrEndFrame", instance_);
}

void FlutterXrApp::Shutdown() {
    if (flutterEngine_ != nullptr && pointerAdded_) {
        SendFlutterPointerEvent(kRemove, lastPointerX_, lastPointerY_, 0);
        pointerAdded_ = false;
    }

    if (flutterEngine_ != nullptr) {
        const FlutterEngineResult shutdownResult = FlutterEngineShutdown(flutterEngine_);
        if (shutdownResult != kSuccess) {
            std::cerr << "[warn] FlutterEngineShutdown failed. result=" << static_cast<int32_t>(shutdownResult) << "\n";
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

    if (backgroundSwapchain_ != XR_NULL_HANDLE) {
        xrDestroySwapchain(backgroundSwapchain_);
        backgroundSwapchain_ = XR_NULL_HANDLE;
    }

    if (pointerRaySwapchain_ != XR_NULL_HANDLE) {
        xrDestroySwapchain(pointerRaySwapchain_);
        pointerRaySwapchain_ = XR_NULL_HANDLE;
    }

    if (pointerSpace_ != XR_NULL_HANDLE) {
        xrDestroySpace(pointerSpace_);
        pointerSpace_ = XR_NULL_HANDLE;
    }

    if (leftPointerSpace_ != XR_NULL_HANDLE) {
        xrDestroySpace(leftPointerSpace_);
        leftPointerSpace_ = XR_NULL_HANDLE;
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
    backgroundTexture_.Reset();
    pointerRayTexture_.Reset();
    quadImages_.clear();
    backgroundImages_.clear();
    pointerRayImages_.clear();
    convertedPixels_.clear();
    backgroundCustomPixels_.clear();
}

}  // namespace flutter_xr


