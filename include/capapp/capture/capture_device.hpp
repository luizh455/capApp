#pragma once

#include <capapp/frame.hpp>
#include <string>
#include <cstdint>

namespace capapp::capture {

struct CaptureDeviceConfig {
    uint32_t targetWidth{1920};
    uint32_t targetHeight{1080};
    uint32_t targetFps{60};
    PixelFormat preferredFormat{PixelFormat::BGRA8};
    bool enableZeroCopy{true};
    std::string deviceName{"Default Capture Device"};
};

struct CaptureDeviceStats {
    uint64_t framesCaptured{0};
    uint64_t framesDropped{0};
    TimestampNs lastCaptureTimestampNs{0};
    double averageFps{0.0};
};

class ICaptureDevice {
public:
    virtual ~ICaptureDevice() = default;

    virtual bool open(const CaptureDeviceConfig& config) = 0;
    virtual void close() noexcept = 0;
    virtual bool is_open() const noexcept = 0;

    virtual bool start_stream() = 0;
    virtual void stop_stream() noexcept = 0;
    virtual bool is_streaming() const noexcept = 0;

    virtual bool acquire_frame(Frame& outFrame, uint32_t timeoutMs = 16) = 0;
    virtual void release_frame(Frame& frame) noexcept = 0;

    virtual CaptureDeviceStats stats() const noexcept = 0;
};

} // namespace capapp::capture
