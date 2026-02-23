#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "flutter_embedder.h"

namespace {

constexpr size_t kSurfaceWidth = 1280;
constexpr size_t kSurfaceHeight = 720;
constexpr DWORD kFirstFrameTimeoutMs = 15000;

std::filesystem::path GetExecutableDir() {
  std::wstring buffer(MAX_PATH, L'\0');
  const DWORD copied =
      GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
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
  const int needed =
      WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return std::string();
  }
  std::string out(static_cast<size_t>(needed - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), needed - 1, nullptr,
                      nullptr);
  return out;
}

struct AppState {
  HANDLE first_frame_event = nullptr;
  std::atomic<uint64_t> frame_count{0};
  std::atomic<size_t> frame_width{0};
  std::atomic<size_t> frame_height{0};
};

bool OnSurfacePresent(void* user_data,
                      const void* allocation,
                      size_t row_bytes,
                      size_t height) {
  if (user_data == nullptr || allocation == nullptr || row_bytes == 0 || height == 0) {
    return false;
  }

  auto* state = static_cast<AppState*>(user_data);
  state->frame_width.store(row_bytes / 4, std::memory_order_relaxed);
  state->frame_height.store(height, std::memory_order_relaxed);

  const uint64_t frame_index =
      state->frame_count.fetch_add(1, std::memory_order_relaxed) + 1;
  if (frame_index == 1 && state->first_frame_event != nullptr) {
    SetEvent(state->first_frame_event);
  }
  return true;
}

}  // namespace

int main() {
  const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool com_initialized = SUCCEEDED(com_result);

  const auto exe_dir = GetExecutableDir();
  const auto assets_dir = exe_dir / "data" / "flutter_assets";
  const auto icu_path = exe_dir / "icudtl.dat";

  if (!std::filesystem::exists(assets_dir / "kernel_blob.bin")) {
    std::cerr << "[error] Missing Flutter assets: " << (assets_dir / "kernel_blob.bin")
              << "\n";
    if (com_initialized) {
      CoUninitialize();
    }
    return 1;
  }

  const std::string assets_utf8 = WideToUtf8(assets_dir.wstring());
  const std::string icu_utf8 =
      std::filesystem::exists(icu_path) ? WideToUtf8(icu_path.wstring()) : std::string();
  if (icu_utf8.empty()) {
    std::cout << "[warn] icudtl.dat was not found next to the executable. "
                 "Trying to run without explicit ICU path.\n";
  }

  AppState app;
  app.first_frame_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (app.first_frame_event == nullptr) {
    std::cerr << "[error] Failed to create frame event.\n";
    if (com_initialized) {
      CoUninitialize();
    }
    return 1;
  }

  FlutterRendererConfig renderer_config{};
  renderer_config.type = kSoftware;
  renderer_config.software.struct_size = sizeof(FlutterSoftwareRendererConfig);
  renderer_config.software.surface_present_callback = OnSurfacePresent;

  const char* switches[] = {"flutter_xr_offscreen", "--enable-impeller=false"};

  FlutterProjectArgs project_args{};
  project_args.struct_size = sizeof(FlutterProjectArgs);
  project_args.assets_path = assets_utf8.c_str();
  project_args.icu_data_path = icu_utf8.empty() ? nullptr : icu_utf8.c_str();
  project_args.command_line_argc = static_cast<int>(std::size(switches));
  project_args.command_line_argv = switches;

  FlutterEngine engine = nullptr;
  const FlutterEngineResult run_result =
      FlutterEngineRun(FLUTTER_ENGINE_VERSION, &renderer_config, &project_args, &app, &engine);
  if (run_result != kSuccess || engine == nullptr) {
    std::cerr << "[error] FlutterEngineRun failed. result=" << run_result << "\n";
    CloseHandle(app.first_frame_event);
    if (com_initialized) {
      CoUninitialize();
    }
    return 1;
  }

  FlutterWindowMetricsEvent metrics{};
  metrics.struct_size = sizeof(metrics);
  metrics.width = kSurfaceWidth;
  metrics.height = kSurfaceHeight;
  metrics.pixel_ratio = 1.0;
  metrics.view_id = 0;

  const FlutterEngineResult metrics_result =
      FlutterEngineSendWindowMetricsEvent(engine, &metrics);
  if (metrics_result != kSuccess) {
    std::cerr << "[error] FlutterEngineSendWindowMetricsEvent failed. result="
              << metrics_result << "\n";
    FlutterEngineShutdown(engine);
    CloseHandle(app.first_frame_event);
    if (com_initialized) {
      CoUninitialize();
    }
    return 1;
  }

  std::cout << "[info] Waiting for first Flutter software frame...\n";
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(kFirstFrameTimeoutMs);

  DWORD wait_result = WAIT_TIMEOUT;
  while (std::chrono::steady_clock::now() < deadline) {
    wait_result = WaitForSingleObject(app.first_frame_event, 10);
    if (wait_result == WAIT_OBJECT_0) {
      break;
    }
  }

  if (wait_result == WAIT_OBJECT_0) {
    std::cout << "[info] First frame callback received. size="
              << app.frame_width.load(std::memory_order_relaxed) << "x"
              << app.frame_height.load(std::memory_order_relaxed)
              << " frames=" << app.frame_count.load(std::memory_order_relaxed) << "\n";
  } else {
    std::cerr << "[error] Timed out waiting for first frame callback.\n";
  }

  const FlutterEngineResult shutdown_result = FlutterEngineShutdown(engine);
  if (shutdown_result != kSuccess) {
    std::cerr << "[error] FlutterEngineShutdown failed. result=" << shutdown_result << "\n";
  }

  CloseHandle(app.first_frame_event);
  if (com_initialized) {
    CoUninitialize();
  }
  return (wait_result == WAIT_OBJECT_0 && shutdown_result == kSuccess) ? 0 : 1;
}
