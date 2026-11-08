#pragma once

#include <capapp/capture/capture_device.hpp>
#include <memory>
#include <string>
#include <cstdint>

namespace capapp::capture {

/**
 * @brief Media Foundation-backed real camera capture device.
 *
 * Uses IMFSourceReader to read frames from a physical camera identified by its
 * MF symbolic link (e.g. L"\\\\?\\usb#vid_...").  Requests ARGB32 output from
 * the reader so that no manual YUY2/NV12 conversion is needed; MF's built-in
 * color converter (MFT) performs the transform on the caller thread.
 *
 * Frame lifecycle:
 *   open()         → creates IMFSourceReader, sets output media type to ARGB32
 *   start_stream() → marks streaming_ = true (reader is already in pull mode)
 *   acquire_frame()→ calls ReadSample (synchronous), locks IMFMediaBuffer,
 *                    and exposes its pixels directly in outFrame.data
 *   release_frame()→ leaves the buffer locked until the next acquire_frame()
 *   close()        → releases reader / COM objects
 */
class MFCaptureDevice final : public ICaptureDevice {
public:
    explicit MFCaptureDevice(std::wstring symbolicLink);
    ~MFCaptureDevice() override;

    // Non-copyable
    MFCaptureDevice(const MFCaptureDevice&) = delete;
    MFCaptureDevice& operator=(const MFCaptureDevice&) = delete;

    bool open(const CaptureDeviceConfig& config) override;
    void close() noexcept override;
    bool is_open() const noexcept override;

    bool start_stream() override;
    void stop_stream() noexcept override;
    bool is_streaming() const noexcept override;

    bool acquire_frame(Frame& outFrame, uint32_t timeoutMs = 16) override;
    void release_frame(Frame& frame) noexcept override;

    CaptureDeviceStats stats() const noexcept override;
    CaptureStreamInfo stream_info() const override;

private:
    std::wstring symbolicLink_;
    CaptureDeviceConfig config_{};
    bool open_{false};
    bool streaming_{false};
    uint64_t sequenceCounter_{0};
    CaptureDeviceStats stats_{};
    CaptureStreamInfo streamInfo_{};

    // Locked until the next acquire, so presentation can upload without a CPU staging copy.
    void* pLockedBuffer_{nullptr}; // IMFMediaBuffer*
    uint32_t frameWidth_{0};
    uint32_t frameHeight_{0};

    // Opaque COM pointers stored as void* to avoid including MF headers in this header
    void* pReader_{nullptr};   // IMFSourceReader*
};

/**
 * @brief Factory: create an MFCaptureDevice with a known symbolic link,
 *        or fall back to DesktopCaptureDevice if symbolicLink is empty.
 */
std::unique_ptr<ICaptureDevice> create_capture_device(const std::wstring& symbolicLink = {});

/** Compatibility overload for benchmark / test code that has no symbolic link. */
std::unique_ptr<ICaptureDevice> create_capture_device();

} // namespace capapp::capture
