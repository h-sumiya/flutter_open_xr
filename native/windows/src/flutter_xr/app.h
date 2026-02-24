#pragma once

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "flutter_embedder.h"
#include "flutter_xr/shared.h"

namespace flutter_xr {

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
    bool hasPose = false;
    bool onQuad = false;
    float hitDistanceMeters = 0.0f;
    XrVector3f rayOriginWorld{0.0f, 0.0f, 0.0f};
    XrVector3f rayDirectionWorld{0.0f, 0.0f, -1.0f};
    XrQuaternionf pointerOrientation{0.0f, 0.0f, 0.0f, 1.0f};
    double xPixels = static_cast<double>(kFlutterSurfaceWidth) * 0.5;
    double yPixels = static_cast<double>(kFlutterSurfaceHeight) * 0.5;
};

class FlutterXrApp {
   public:
    ~FlutterXrApp();

    void Initialize();
    void Run();
    bool HandleFlutterSurfacePresent(const void* allocation, size_t rowBytes, size_t height);
    void HandleFlutterPlatformMessage(const FlutterPlatformMessage* message);

   private:
    enum class BackgroundMode : uint8_t {
        None,
        GroundGrid,
        Dds,
        Glb,
    };

    void CreateInstance();
    void InitializeSystem();
    void InitializeD3D11Device();
    void CreateSession();
    void CreateReferenceSpace();

    void SuggestBindings(XrPath interactionProfile, const std::vector<XrActionSuggestedBinding>& bindings);
    void InitializeInputActions();
    PointerHitResult QueryPointerHit(XrTime predictedDisplayTime, XrSpace pointerSpace, XrPath handPath);
    bool SendFlutterPointerEvent(FlutterPointerPhase phase, double xPixels, double yPixels, int64_t buttons);
    void EnsureFlutterPointerAdded(double xPixels, double yPixels);
    void PollInput(XrTime predictedDisplayTime);

    void CreateQuadSwapchain();
    void CreateBackgroundSwapchain();
    void CreatePointerRaySwapchain();
    void CreateFlutterTexture();
    void CreateBackgroundTexture();
    void CreatePointerRayTexture();

    void InitializeFlutterEngine();
    bool UploadLatestFlutterFrame();
    bool IsBackgroundEnabled();
    bool UploadBackgroundTexture();
    std::string HandleBackgroundMessage(const std::string& message);

    void PollEvents();
    void HandleSessionStateChanged(const XrEventDataSessionStateChanged& changed);
    void RenderFrame();
    void Shutdown();

    XrInstance instance_{XR_NULL_HANDLE};
    XrSystemId systemId_{XR_NULL_SYSTEM_ID};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace appSpace_{XR_NULL_HANDLE};
    XrSpace pointerSpace_{XR_NULL_HANDLE};
    XrSpace leftPointerSpace_{XR_NULL_HANDLE};
    XrSwapchain quadSwapchain_{XR_NULL_HANDLE};
    XrSwapchain backgroundSwapchain_{XR_NULL_HANDLE};
    XrSwapchain pointerRaySwapchain_{XR_NULL_HANDLE};
    XrActionSet inputActionSet_{XR_NULL_HANDLE};
    XrAction pointerPoseAction_{XR_NULL_HANDLE};
    XrAction triggerValueAction_{XR_NULL_HANDLE};
    XrPath rightHandPath_{XR_NULL_PATH};
    XrPath leftHandPath_{XR_NULL_PATH};

    XrViewConfigurationType viewConfigType_{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    XrEnvironmentBlendMode blendMode_{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    XrSessionState sessionState_{XR_SESSION_STATE_UNKNOWN};

    bool sessionRunning_{false};
    bool exitRequested_{false};
    bool triggerPressed_{false};
    bool pointerAdded_{false};
    bool pointerDown_{false};
    bool pointerRayVisible_{false};
    bool leftPointerRayVisible_{false};
    float pointerRayLengthMeters_{0.0f};
    float leftPointerRayLengthMeters_{0.0f};
    XrPosef pointerRayPose_{};
    XrPosef leftPointerRayPose_{};
    double lastPointerX_{static_cast<double>(kFlutterSurfaceWidth) * 0.5};
    double lastPointerY_{static_cast<double>(kFlutterSurfaceHeight) * 0.5};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> deviceContext_;
    DXGI_FORMAT colorFormat_{DXGI_FORMAT_R8G8B8A8_UNORM};
    bool isBgraFormat_{false};

    std::vector<XrSwapchainImageD3D11KHR> quadImages_;
    std::vector<XrSwapchainImageD3D11KHR> backgroundImages_;
    std::vector<XrSwapchainImageD3D11KHR> pointerRayImages_;
    ComPtr<ID3D11Texture2D> flutterTexture_;
    ComPtr<ID3D11Texture2D> backgroundTexture_;
    ComPtr<ID3D11Texture2D> pointerRayTexture_;
    std::mutex backgroundMutex_;
    BackgroundMode backgroundMode_{BackgroundMode::GroundGrid};
    std::string backgroundAssetPathUtf8_;
    std::vector<uint32_t> backgroundCustomPixels_;
    uint64_t backgroundConfigVersion_{1};
    uint64_t backgroundUploadedVersion_{0};
    FlutterEngine flutterEngine_{nullptr};
    FlutterBridgeState flutterBridge_;
    uint64_t uploadedFrameIndex_{0};
    std::vector<uint8_t> convertedPixels_;
    std::string assetsPathUtf8_;
    std::string icuPathUtf8_;
};

}  // namespace flutter_xr
