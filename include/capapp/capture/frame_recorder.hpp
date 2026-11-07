#pragma once

#include <capapp/frame.hpp>
#include <capapp/ring_buffer.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>

namespace capapp::capture {

struct FrameRecorderStats {
    uint64_t totalSnapshotsRequested{0};
    uint64_t totalSnapshotsSaved{0};
    uint64_t totalFailedSaves{0};
    std::string lastSavedPath;
};

class FrameRecorder {
public:
    FrameRecorder();
    ~FrameRecorder();

    // Non-copyable
    FrameRecorder(const FrameRecorder&) = delete;
    FrameRecorder& operator=(const FrameRecorder&) = delete;

    /**
     * @brief Starts the background worker thread for disk serialization.
     */
    bool start();

    /**
     * @brief Stops the background worker thread.
     */
    void stop();

    /**
     * @brief Asynchronously enqueues a video frame to be saved as an uncompressed image (PPM/BMP).
     * Non-blocking call for ultra-low latency.
     */
    bool capture_snapshot(const Frame& frame, std::string filename = "");

    /**
     * @brief Synchronous helper to save a frame to PPM image file.
     */
    static bool save_ppm_image(const std::string& filepath, uint32_t width, uint32_t height, const uint8_t* bgraData) noexcept;

    [[nodiscard]] FrameRecorderStats stats() const noexcept;
    [[nodiscard]] bool is_active() const noexcept;

private:
    struct PendingSnapshot {
        uint32_t width{0};
        uint32_t height{0};
        TimestampNs ptsNs{0};
        std::string filename;
        std::vector<uint8_t> pixelData;
    };

    void worker_loop();

    std::atomic<bool> running_{false};
    std::thread workerThread_;
    RingBuffer<PendingSnapshot> queue_{64};

    std::atomic<uint64_t> requested_{0};
    std::atomic<uint64_t> saved_{0};
    std::atomic<uint64_t> failed_{0};
    std::string lastSavedPath_;
};

} // namespace capapp::capture
