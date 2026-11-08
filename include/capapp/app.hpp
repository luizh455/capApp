#pragma once

#include <capapp/ui/window.hpp>
#include <capapp/ui/d3d11_renderer.hpp>
#include <capapp/vulkan/vulkan_context.hpp>
#include <capapp/audio/wasapi_engine.hpp>
#include <capapp/capture/capture_device.hpp>
#include <capapp/capture/camera_selector.hpp>
#include <capapp/capture/frame_recorder.hpp>
#include <capapp/sync/av_sync_pipeline.hpp>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace capapp {

struct AppOptions {
    bool startFullscreen{false};
    bool showHud{true};
    bool headlessBenchmark{false};
    uint32_t targetFps{60};
    uint32_t targetWidth{1920};
    uint32_t targetHeight{1080};
    uint32_t cameraIndex{0};
    std::string cameraName;
    bool cameraSpecified{false};
    bool resolutionSpecified{false};
    bool fpsSpecified{false};
    bool fullscreenSpecified{false};
    bool hudSpecified{false};
};

class CapApplication {
public:
    explicit CapApplication(AppOptions options = {});
    ~CapApplication();

    // Non-copyable
    CapApplication(const CapApplication&) = delete;
    CapApplication& operator=(const CapApplication&) = delete;

    bool initialize();
    int run();

    void toggle_hud() noexcept;
    void toggle_pause() noexcept;
    void cycle_camera() noexcept;
    void cycle_resolution() noexcept;
    void cycle_fps() noexcept;
    void toggle_scaling_filter() noexcept;
    void cycle_capture_compression(int direction = 1) noexcept;
    void take_snapshot() noexcept;
    void select_home_option(int option) noexcept;

private:
    struct CaptureRequest {
        std::wstring symbolicLink;
        capture::CaptureDeviceConfig config;
    };

    void render_frame();
    void render_hud(HDC hdc, int width, int height);
    void render_monitoring_info(HDC hdc, int top);
    bool initialize_runtime();
    void configure_window_callbacks();
    void draw_startup_splash();
    void queue_capture_reconfiguration(CaptureRequest request);
    void capture_reconfiguration_loop();
    void stop_capture_reconfiguration_worker() noexcept;
    void save_settings() const noexcept;
    [[nodiscard]] std::vector<std::wstring> home_option_choices(int option) const;
    [[nodiscard]] int current_option_choice(int option) const;
    void open_option_panel();
    void apply_option_choice(int option, size_t choice);

    AppOptions options_;
    std::unique_ptr<ui::NativeWindow> window_;
    std::unique_ptr<ui::D3D11Renderer> d3dRenderer_;
    std::unique_ptr<vulkan::VulkanContext> vulkanCtx_;
    std::unique_ptr<audio::WasapiAudioEngine> audioEngine_;
    std::unique_ptr<capture::ICaptureDevice> captureDev_;
    std::unique_ptr<capture::CameraSelector> cameraSelector_;
    std::unique_ptr<capture::FrameRecorder> frameRecorder_;
    std::unique_ptr<sync::AVSyncPipeline> avSync_;
    std::mutex captureDeviceMutex_;
    std::mutex lastFrameMutex_;
    std::mutex captureRequestMutex_;
    std::condition_variable captureRequestCv_;
    std::optional<CaptureRequest> pendingCaptureRequest_;
    std::thread captureReconfigurationThread_;
    std::thread startupThread_;
    std::atomic<bool> captureReconfiguring_{false};
    std::atomic<bool> startupComplete_{false};
    std::atomic<bool> startupSucceeded_{false};
    bool stopCaptureReconfiguration_{false};

    ui::ScalingMode scalingMode_{ui::ScalingMode::Lanczos3};
    capture::CaptureCompression captureCompression_{capture::CaptureCompression::Auto};
    bool isPaused_{false};
    int selectedHomeOption_{0};
    int selectedOptionChoice_{0};
    bool optionPanelOpen_{false};
    bool showNegotiatedInfo_{false};
    uint64_t frameCount_{0};
    double currentFps_{60.0};
    double captureAcquireMs_{0.0};
    double renderFrameMs_{0.0};
    capture::CaptureStreamInfo captureStreamInfo_{};
    TimestampNs lastFpsUpdateNs_{0};
    Frame lastFrame_{}; // most-recently captured raw frame for rendering

#if defined(_WIN32)
    ULONG_PTR gdiplusToken_{0};
    HBITMAP startupSplashBitmap_{nullptr};
    HDC memDC_{nullptr};
    HBITMAP memBitmap_{nullptr};
    HBITMAP oldBitmap_{nullptr};
    int backbufferWidth_{0};
    int backbufferHeight_{0};
#endif
};

} // namespace capapp
