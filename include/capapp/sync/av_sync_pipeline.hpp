#pragma once

#include <capapp/frame.hpp>
#include <capapp/audio_buffer.hpp>
#include <capapp/ring_buffer.hpp>
#include <capapp/vulkan/vulkan_context.hpp>
#include <cmath>
#include <cstdint>
#include <memory>

namespace capapp::sync {

struct SyncStats {
    uint64_t matchedAVPairs{0};
    uint64_t droppedVideoFrames{0};
    uint64_t droppedAudioBuffers{0};
    int64_t currentDriftNs{0};
    TimestampNs estimatedGlassToGlassLatencyNs{0};
};

/**
 * @brief High-precision Audio/Video PTS Synchronization and Jitter Buffer.
 *
 * Aligns video frames with WASAPI audio samples using nanosecond PTS timestamps.
 * Supports zero-copy surface import directly into Vulkan textures.
 */
class AVSyncPipeline {
public:
    explicit AVSyncPipeline(vulkan::VulkanContext* vulkanCtx = nullptr);
    ~AVSyncPipeline();

    // Non-copyable
    AVSyncPipeline(const AVSyncPipeline&) = delete;
    AVSyncPipeline& operator=(const AVSyncPipeline&) = delete;

    /**
     * @brief Pushes a newly captured video frame into the video queue.
     */
    bool push_video_frame(const Frame& frame);

    /**
     * @brief Pushes a newly captured audio buffer into the audio queue.
     */
    bool push_audio_buffer(const AudioBuffer& audio);

    /**
     * @brief Performs zero-copy Vulkan surface import for hardware textures.
     */
    bool import_zero_copy_surface(Frame& frame);

    /**
     * @brief Synchronizes and pairs the nearest audio buffer and video frame.
     */
    bool get_synchronized_pair(Frame& outVideo, AudioBuffer& outAudio, int64_t maxAllowedSkewNs = 20'000'000);

    [[nodiscard]] SyncStats stats() const noexcept;
    [[nodiscard]] size_t pending_video_frames() const noexcept;
    [[nodiscard]] size_t pending_audio_buffers() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace capapp::sync
