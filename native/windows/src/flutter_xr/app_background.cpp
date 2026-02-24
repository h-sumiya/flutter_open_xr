#include "flutter_xr/app.h"

#include <wincodec.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

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

std::string TrimAscii(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) {
        return std::string();
    }
    return std::string(begin, end);
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool BuildGroundGridPixels(bool bgraFormat, std::vector<uint32_t>* outPixels) {
    if (outPixels == nullptr) {
        return false;
    }

    const size_t width = static_cast<size_t>(kBackgroundTextureWidth);
    const size_t height = static_cast<size_t>(kBackgroundTextureHeight);
    outPixels->assign(width * height, 0);

    constexpr int kMajorCell = 128;
    constexpr int kMinorCell = 32;
    constexpr int kMajorThickness = 3;
    constexpr int kMinorThickness = 1;

    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const bool majorLine = (static_cast<int>(x) % kMajorCell) < kMajorThickness ||
                                   (static_cast<int>(y) % kMajorCell) < kMajorThickness;
            const bool minorLine = (static_cast<int>(x) % kMinorCell) < kMinorThickness ||
                                   (static_cast<int>(y) % kMinorCell) < kMinorThickness;

            const float u = (static_cast<float>(x) / static_cast<float>(width - 1)) * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) / static_cast<float>(height - 1)) * 2.0f - 1.0f;
            const float radial = std::sqrt(u * u + v * v);
            const float fade = std::clamp(1.2f - radial, 0.35f, 1.0f);

            uint8_t r = static_cast<uint8_t>(24.0f * fade);
            uint8_t g = static_cast<uint8_t>(30.0f * fade);
            uint8_t b = static_cast<uint8_t>(38.0f * fade);
            if (minorLine) {
                r = static_cast<uint8_t>(56.0f * fade);
                g = static_cast<uint8_t>(72.0f * fade);
                b = static_cast<uint8_t>(90.0f * fade);
            }
            if (majorLine) {
                r = static_cast<uint8_t>(95.0f * fade);
                g = static_cast<uint8_t>(140.0f * fade);
                b = static_cast<uint8_t>(175.0f * fade);
            }

            (*outPixels)[y * width + x] = PackColor(r, g, b, 255, bgraFormat);
        }
    }

    return true;
}

bool ResolveExistingFilePath(const std::string& utf8Path, std::filesystem::path* outPath, std::string* outError) {
    if (outPath == nullptr) {
        return false;
    }

    const std::string trimmed = TrimAscii(utf8Path);
    if (trimmed.empty()) {
        if (outError != nullptr) {
            *outError = "Background file path is empty.";
        }
        return false;
    }

    std::filesystem::path inputPath(Utf8ToWide(trimmed));
    if (inputPath.empty()) {
        if (outError != nullptr) {
            *outError = "Background file path could not be parsed.";
        }
        return false;
    }

    std::error_code ec;
    auto isValidFile = [&](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate, ec) && !ec && std::filesystem::is_regular_file(candidate, ec) && !ec;
    };

    if (inputPath.is_absolute()) {
        if (!isValidFile(inputPath)) {
            if (outError != nullptr) {
                *outError = "Background file was not found: " + trimmed;
            }
            return false;
        }
        *outPath = inputPath;
        return true;
    }

    const std::filesystem::path absoluteFromCwd = std::filesystem::absolute(inputPath, ec);
    if (!ec && isValidFile(absoluteFromCwd)) {
        *outPath = absoluteFromCwd;
        return true;
    }

    const std::filesystem::path absoluteFromExe = GetExecutableDir() / inputPath;
    if (isValidFile(absoluteFromExe)) {
        *outPath = absoluteFromExe;
        return true;
    }

    if (outError != nullptr) {
        *outError = "Background file was not found: " + trimmed;
    }
    return false;
}

bool DecodeImageFileToPixels(const std::wstring& sourcePath,
                             bool bgraFormat,
                             std::vector<uint32_t>* outPixels,
                             std::string* outError) {
    if (outPixels == nullptr || sourcePath.empty()) {
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    }
    if (FAILED(hr)) {
        if (outError != nullptr) {
            *outError = "Failed to create WIC imaging factory (" + HResultToString(hr) + ").";
        }
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(sourcePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                            decoder.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        if (outError != nullptr) {
            *outError = "Failed to open background image (" + HResultToString(hr) + ").";
        }
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        if (outError != nullptr) {
            *outError = "Failed to decode background image frame (" + HResultToString(hr) + ").";
        }
        return false;
    }

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    hr = frame->GetSize(&sourceWidth, &sourceHeight);
    if (FAILED(hr) || sourceWidth == 0 || sourceHeight == 0) {
        if (outError != nullptr) {
            *outError = "Background image size is invalid.";
        }
        return false;
    }

    ComPtr<IWICBitmapSource> sourceBitmap;
    if (sourceWidth != static_cast<UINT>(kBackgroundTextureWidth) ||
        sourceHeight != static_cast<UINT>(kBackgroundTextureHeight)) {
        ComPtr<IWICBitmapScaler> scaler;
        hr = factory->CreateBitmapScaler(scaler.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            if (outError != nullptr) {
                *outError = "Failed to create WIC scaler (" + HResultToString(hr) + ").";
            }
            return false;
        }

        hr = scaler->Initialize(frame.Get(), static_cast<UINT>(kBackgroundTextureWidth),
                                static_cast<UINT>(kBackgroundTextureHeight), WICBitmapInterpolationModeFant);
        if (FAILED(hr)) {
            if (outError != nullptr) {
                *outError = "Failed to scale background image (" + HResultToString(hr) + ").";
            }
            return false;
        }
        sourceBitmap = scaler;
    } else {
        sourceBitmap = frame;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        if (outError != nullptr) {
            *outError = "Failed to create WIC format converter (" + HResultToString(hr) + ").";
        }
        return false;
    }

    hr = converter->Initialize(sourceBitmap.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        if (outError != nullptr) {
            *outError = "Failed to convert background image to RGBA (" + HResultToString(hr) + ").";
        }
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(kBackgroundTextureWidth) * static_cast<size_t>(kBackgroundTextureHeight);
    const size_t rowBytes = static_cast<size_t>(kBackgroundTextureWidth) * 4;
    const size_t byteCount = pixelCount * 4;
    if (rowBytes > std::numeric_limits<UINT>::max() || byteCount > std::numeric_limits<UINT>::max()) {
        if (outError != nullptr) {
            *outError = "Background image is too large.";
        }
        return false;
    }

    std::vector<uint8_t> rgbaPixels(byteCount);
    hr = converter->CopyPixels(nullptr, static_cast<UINT>(rowBytes), static_cast<UINT>(byteCount), rgbaPixels.data());
    if (FAILED(hr)) {
        if (outError != nullptr) {
            *outError = "Failed to read background pixels (" + HResultToString(hr) + ").";
        }
        return false;
    }

    outPixels->resize(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t r = rgbaPixels[i * 4 + 0];
        const uint8_t g = rgbaPixels[i * 4 + 1];
        const uint8_t b = rgbaPixels[i * 4 + 2];
        const uint8_t a = rgbaPixels[i * 4 + 3];
        (*outPixels)[i] = PackColor(r, g, b, a, bgraFormat);
    }
    return true;
}

}  // namespace

void FlutterXrApp::CreateBackgroundSwapchain() {
    XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainCreateInfo.createFlags = 0;
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.format = static_cast<int64_t>(colorFormat_);
    swapchainCreateInfo.sampleCount = 1;
    swapchainCreateInfo.width = kBackgroundTextureWidth;
    swapchainCreateInfo.height = kBackgroundTextureHeight;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 1;
    swapchainCreateInfo.mipCount = 1;

    ThrowIfXrFailed(xrCreateSwapchain(session_, &swapchainCreateInfo, &backgroundSwapchain_), "xrCreateSwapchain(background)",
                    instance_);

    uint32_t imageCount = 0;
    ThrowIfXrFailed(xrEnumerateSwapchainImages(backgroundSwapchain_, 0, &imageCount, nullptr),
                    "xrEnumerateSwapchainImages(background count)", instance_);
    if (imageCount == 0) {
        throw std::runtime_error("Runtime returned zero background swapchain images.");
    }

    backgroundImages_.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    ThrowIfXrFailed(
        xrEnumerateSwapchainImages(backgroundSwapchain_, imageCount, &imageCount,
                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(backgroundImages_.data())),
        "xrEnumerateSwapchainImages(background data)", instance_);
}

void FlutterXrApp::CreateBackgroundTexture() {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(kBackgroundTextureWidth);
    desc.Height = static_cast<UINT>(kBackgroundTextureHeight);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = colorFormat_;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    ThrowIfFailed(device_->CreateTexture2D(&desc, nullptr, backgroundTexture_.ReleaseAndGetAddressOf()),
                  "ID3D11Device::CreateTexture2D(backgroundTexture)");

    std::vector<uint32_t> initialPixels;
    if (!BuildGroundGridPixels(isBgraFormat_, &initialPixels)) {
        throw std::runtime_error("Failed to initialize background texture pixels.");
    }

    deviceContext_->UpdateSubresource(backgroundTexture_.Get(), 0, nullptr, initialPixels.data(),
                                      static_cast<UINT>(kBackgroundTextureWidth * sizeof(uint32_t)), 0);
    backgroundUploadedVersion_ = 0;
}

bool FlutterXrApp::IsBackgroundEnabled() {
    std::lock_guard<std::mutex> lock(backgroundMutex_);
    return backgroundMode_ != BackgroundMode::None;
}

bool FlutterXrApp::UploadBackgroundTexture() {
    BackgroundMode mode = BackgroundMode::GroundGrid;
    uint64_t targetVersion = 0;
    std::vector<uint32_t> pixels;

    {
        std::lock_guard<std::mutex> lock(backgroundMutex_);
        targetVersion = backgroundConfigVersion_;
        if (backgroundUploadedVersion_ == targetVersion) {
            return true;
        }

        mode = backgroundMode_;
        if (mode == BackgroundMode::Dds) {
            pixels = backgroundCustomPixels_;
        }
    }

    if (mode == BackgroundMode::None) {
        std::lock_guard<std::mutex> lock(backgroundMutex_);
        if (backgroundConfigVersion_ == targetVersion) {
            backgroundUploadedVersion_ = targetVersion;
        }
        return true;
    }

    if (mode == BackgroundMode::GroundGrid) {
        if (!BuildGroundGridPixels(isBgraFormat_, &pixels)) {
            return false;
        }
    } else if (mode == BackgroundMode::Dds) {
        if (pixels.empty()) {
            return false;
        }
    } else {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(backgroundMutex_);
        if (backgroundConfigVersion_ != targetVersion) {
            return false;
        }
    }

    deviceContext_->UpdateSubresource(backgroundTexture_.Get(), 0, nullptr, pixels.data(),
                                      static_cast<UINT>(kBackgroundTextureWidth * sizeof(uint32_t)), 0);

    std::lock_guard<std::mutex> lock(backgroundMutex_);
    if (backgroundConfigVersion_ == targetVersion) {
        backgroundUploadedVersion_ = targetVersion;
    }
    return true;
}

std::string FlutterXrApp::HandleBackgroundMessage(const std::string& message) {
    const std::string trimmed = TrimAscii(message);
    if (trimmed.empty()) {
        return "error:background command is empty.";
    }

    const size_t separator = trimmed.find('|');
    const std::string command = ToLowerAscii(TrimAscii(trimmed.substr(0, separator)));
    const std::string argument = separator == std::string::npos ? std::string() : TrimAscii(trimmed.substr(separator + 1));

    if (command == "none") {
        std::lock_guard<std::mutex> lock(backgroundMutex_);
        backgroundMode_ = BackgroundMode::None;
        backgroundAssetPathUtf8_.clear();
        backgroundCustomPixels_.clear();
        backgroundConfigVersion_ += 1;
        return "ok";
    }

    if (command == "grid") {
        std::lock_guard<std::mutex> lock(backgroundMutex_);
        backgroundMode_ = BackgroundMode::GroundGrid;
        backgroundAssetPathUtf8_.clear();
        backgroundCustomPixels_.clear();
        backgroundConfigVersion_ += 1;
        return "ok";
    }

    if (command == "dds") {
        std::filesystem::path resolvedPath;
        std::string resolveError;
        if (!ResolveExistingFilePath(argument, &resolvedPath, &resolveError)) {
            return "error:" + resolveError;
        }

        const std::string extension = ToLowerAscii(WideToUtf8(resolvedPath.extension().wstring()));
        if (extension != ".dds") {
            return "error:Only .dds files are supported for this command.";
        }

        std::vector<uint32_t> decodedPixels;
        std::string decodeError;
        if (!DecodeImageFileToPixels(resolvedPath.wstring(), isBgraFormat_, &decodedPixels, &decodeError)) {
            return "error:" + decodeError;
        }

        std::lock_guard<std::mutex> lock(backgroundMutex_);
        backgroundMode_ = BackgroundMode::Dds;
        backgroundAssetPathUtf8_ = WideToUtf8(resolvedPath.wstring());
        backgroundCustomPixels_ = std::move(decodedPixels);
        backgroundConfigVersion_ += 1;
        return "ok";
    }

    if (command == "glb") {
        return "error:.glb background is not supported yet.";
    }

    return "error:Unknown background command. Use none, grid, dds|<path>, or glb|<path>.";
}

}  // namespace flutter_xr
