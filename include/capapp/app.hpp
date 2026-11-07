#pragma once

#include <capapp/ui/window.hpp>
#include <capapp/vulkan/vulkan_context.hpp>
#include <capapp/audio/wasapi_engine.hpp>
#include <capapp/capture/capture_device.hpp>
#include <capapp/sync/av_sync_pipeline.hpp>
#include <memory>
#include <string>

namespace capapp {

struct AppOptions {
    bool startFullscreen{false};
    bool showHud{true};
    bool headlessBenchmark{false};
    uint32_t targetFps{60};
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

private:
    void render_frame();
    void render_hud(HDC hdc, int width, int height);

    AppOptions options_;
    std::unique_ptr<ui::NativeWindow> window_;
    std::unique_ptr<vulkan::VulkanContext> vulkanCtx_;
    std::unique_ptr<audio::WasapiAudioEngine> audioEngine_;
    std::unique_ptr<capture::ICaptureDevice> captureDev_;
    std::unique_ptr<sync::AVSyncPipeline> avSync_;

    bool isPaused_{false};
    uint64_t frameCount_{0};
    double currentFps_{60.0};
    TimestampNs lastFpsUpdateNs_{0};

#if defined(_WIN32)
    HDC memDC_{nullptr};
    HBITMAP memBitmap_{nullptr};
    HBITMAP oldBitmap_{nullptr};
    int backbufferWidth_{0};
    int backbufferHeight_{0};
#endif
};

} // namespace capapp
