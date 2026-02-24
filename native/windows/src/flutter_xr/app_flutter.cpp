#include "flutter_xr/app.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace flutter_xr {

namespace {

constexpr const char* kBackgroundChannel = "flutter_open_xr/background";

bool OnSurfacePresent(void* user_data, const void* allocation, size_t row_bytes, size_t height) {
    auto* app = static_cast<FlutterXrApp*>(user_data);
    if (app == nullptr) {
        return false;
    }
    return app->HandleFlutterSurfacePresent(allocation, row_bytes, height);
}

void OnPlatformMessage(const FlutterPlatformMessage* message, void* user_data) {
    auto* app = static_cast<FlutterXrApp*>(user_data);
    if (app == nullptr) {
        return;
    }
    app->HandleFlutterPlatformMessage(message);
}

}  // namespace

void FlutterXrApp::InitializeFlutterEngine() {
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

    const char* commandLineArgs[] = {"flutter_open_xr_runner", "--enable-impeller=false"};
    FlutterProjectArgs projectArgs{};
    projectArgs.struct_size = sizeof(FlutterProjectArgs);
    projectArgs.assets_path = assetsPathUtf8_.c_str();
    projectArgs.icu_data_path = icuPathUtf8_.empty() ? nullptr : icuPathUtf8_.c_str();
    projectArgs.command_line_argc = static_cast<int>(std::size(commandLineArgs));
    projectArgs.command_line_argv = commandLineArgs;
    projectArgs.platform_message_callback = OnPlatformMessage;

    const FlutterEngineResult runResult =
        FlutterEngineRun(FLUTTER_ENGINE_VERSION, &rendererConfig, &projectArgs, this, &flutterEngine_);
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

bool FlutterXrApp::HandleFlutterSurfacePresent(const void* allocation, size_t rowBytes, size_t height) {
    if (allocation == nullptr || rowBytes < 4 || height == 0) {
        return false;
    }

    const size_t frameBytes = rowBytes * height;
    {
        std::lock_guard<std::mutex> lock(flutterBridge_.latestFrame.mutex);
        flutterBridge_.latestFrame.pixels.resize(frameBytes);
        std::memcpy(flutterBridge_.latestFrame.pixels.data(), allocation, frameBytes);
        flutterBridge_.latestFrame.rowBytes = rowBytes;
        flutterBridge_.latestFrame.width = rowBytes / 4;
        flutterBridge_.latestFrame.height = height;
        flutterBridge_.latestFrame.frameIndex += 1;
    }

    if (flutterBridge_.firstFrameEvent != nullptr) {
        SetEvent(flutterBridge_.firstFrameEvent);
    }
    return true;
}

void FlutterXrApp::HandleFlutterPlatformMessage(const FlutterPlatformMessage* message) {
    if (message == nullptr || message->response_handle == nullptr || flutterEngine_ == nullptr) {
        return;
    }

    auto sendResponse = [&](const std::string& responseText) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(responseText.data());
        const FlutterEngineResult responseResult =
            FlutterEngineSendPlatformMessageResponse(flutterEngine_, message->response_handle, bytes, responseText.size());
        if (responseResult != kSuccess) {
            std::cerr << "[warn] FlutterEngineSendPlatformMessageResponse failed. result="
                      << static_cast<int32_t>(responseResult) << "\n";
        }
    };

    if (message->channel == nullptr || std::strcmp(message->channel, kBackgroundChannel) != 0) {
        sendResponse(std::string());
        return;
    }

    std::string command;
    if (message->message != nullptr && message->message_size > 0) {
        command.assign(reinterpret_cast<const char*>(message->message), message->message_size);
    }

    sendResponse(HandleBackgroundMessage(command));
}

bool FlutterXrApp::UploadLatestFlutterFrame() {
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

}  // namespace flutter_xr


