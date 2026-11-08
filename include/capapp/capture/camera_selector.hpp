#pragma once

#include <capapp/frame.hpp>
#include <capapp/capture/capture_device.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace capapp::capture {

struct ResolutionPreset {
    uint32_t width{1920};
    uint32_t height{1080};
    std::string name{"1080p"};
};

struct CameraDeviceInfo {
    uint32_t index{0};
    std::string name;
    std::wstring symbolicLink;
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t maxFps{60};
    PixelFormat preferredFormat{PixelFormat::BGRA8};
    std::vector<CaptureCompression> supportedCompressions{
        CaptureCompression::Auto, CaptureCompression::YUY2, CaptureCompression::MJPEG
    };
    bool isVirtual{false};
};

class CameraSelector {
public:
    CameraSelector();
    ~CameraSelector();

    // Non-copyable
    CameraSelector(const CameraSelector&) = delete;
    CameraSelector& operator=(const CameraSelector&) = delete;

    /**
     * @brief Scans and enumerates all available video capture devices (Media Foundation / DirectShow / Virtual).
     */
    static std::vector<CameraDeviceInfo> enumerate_devices();

    /**
     * @brief Refreshes the internal list of camera inputs.
     */
    void refresh_devices();

    /**
     * @brief Selects active camera input by zero-based index.
     */
    bool select_device(uint32_t index);

    /**
     * @brief Selects active camera input by substring or exact name match.
     */
    bool select_device_by_name(std::string_view name);

    /**
     * @brief Cycles to the next available camera input.
     */
    const CameraDeviceInfo& cycle_next_device();

    /**
     * @brief Cycles to the previous available camera input.
     */
    const CameraDeviceInfo& cycle_prev_device();

    /**
     * @brief Cycles through supported resolution presets (1080p -> 1440p -> 4K).
     */
    ResolutionPreset cycle_resolution() noexcept;

    /**
     * @brief Cycles through target frame rates (30 FPS <-> 60 FPS).
     */
    uint32_t cycle_fps() noexcept;

    bool set_resolution(uint32_t width, uint32_t height) noexcept;
    bool set_fps(uint32_t fps) noexcept;

    [[nodiscard]] ResolutionPreset active_resolution() const noexcept;
    [[nodiscard]] uint32_t active_fps() const noexcept;
    [[nodiscard]] const CameraDeviceInfo* active_device() const noexcept;
    [[nodiscard]] uint32_t active_index() const noexcept;
    [[nodiscard]] size_t device_count() const noexcept;
    [[nodiscard]] const std::vector<CameraDeviceInfo>& devices() const noexcept;

    // Callback invoked when camera device is switched
    std::function<void(const CameraDeviceInfo& newDevice)> onDeviceChanged;

    // Callback invoked when resolution or FPS format is changed
    std::function<void(uint32_t width, uint32_t height, uint32_t fps)> onFormatChanged;

private:
    std::vector<CameraDeviceInfo> devices_;
    uint32_t activeIndex_{0};
    uint32_t activeResIndex_{0};
    uint32_t activeFps_{60};
    std::vector<ResolutionPreset> resolutionPresets_{
        {1920, 1080, "1080p"},
        {2560, 1440, "1440p"},
        {3840, 2160, "4K (2160p)"}
    };
};

} // namespace capapp::capture
