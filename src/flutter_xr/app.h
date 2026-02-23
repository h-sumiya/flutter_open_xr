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
    bool onQuad = false;
    double xPixels = static_cast<double>(kFlutterSurfaceWidth) * 0.5;
    double yPixels = static_cast<double>(kFlutterSurfaceHeight) * 0.5;
};

class FlutterXrApp {
   public:
    ~FlutterXrApp();

    void Initialize();
    void Run();

   private:
    void CreateInstance();
    void InitializeSystem();
    void InitializeD3D11Device();
    void CreateSession();
    void CreateReferenceSpace();

    void SuggestBindings(XrPath interactionProfile, const std::vector<XrActionSuggestedBinding>& bindings);
    void InitializeInputActions();
    PointerHitResult QueryPointerHit(XrTime predictedDisplayTime);
    bool SendFlutterPointerEvent(FlutterPointerPhase phase, double xPixels, double yPixels, int64_t buttons);
    void EnsureFlutterPointerAdded(double xPixels, double yPixels);
    void PollInput(XrTime predictedDisplayTime);

    void CreateQuadSwapchain();
    void CreateFlutterTexture();

    void InitializeFlutterEngine();
    bool UploadLatestFlutterFrame();

    void PollEvents();
    void HandleSessionStateChanged(const XrEventDataSessionStateChanged& changed);
    void RenderFrame();
    void Shutdown();

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

}  // namespace flutter_xr
