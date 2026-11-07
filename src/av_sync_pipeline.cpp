#include <capapp/sync/av_sync_pipeline.hpp>
#include <cmath>
#include <cstdlib>

namespace capapp::sync {

struct AVSyncPipeline::Impl {
    vulkan::VulkanContext* vulkanCtx{nullptr};
    RingBuffer<Frame> videoQueue{64};
    RingBuffer<AudioBuffer> audioQueue{64};
    SyncStats stats{};

    explicit Impl(vulkan::VulkanContext* ctx) : vulkanCtx(ctx) {}
};

AVSyncPipeline::AVSyncPipeline(vulkan::VulkanContext* vulkanCtx)
    : impl_(std::make_unique<Impl>(vulkanCtx)) {}

AVSyncPipeline::~AVSyncPipeline() = default;

bool AVSyncPipeline::push_video_frame(const Frame& frame) {
    if (!impl_->videoQueue.push(frame)) {
        impl_->stats.droppedVideoFrames++;
        return false;
    }
    return true;
}

bool AVSyncPipeline::push_audio_buffer(const AudioBuffer& audio) {
    if (!impl_->audioQueue.push(audio)) {
        impl_->stats.droppedAudioBuffers++;
        return false;
    }
    return true;
}

bool AVSyncPipeline::import_zero_copy_surface(Frame& frame) {
    if (frame.vkResourceHandle != nullptr) {
        return true; // Already imported
    }

    if (impl_->vulkanCtx && impl_->vulkanCtx->is_initialized()) {
        vulkan::VkExtent2D extent{frame.width, frame.height};
        auto img = impl_->vulkanCtx->create_image_resource(extent, vulkan::VK_FORMAT_B8G8R8A8_UNORM);
        if (img.is_valid()) {
            frame.vkResourceHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(img.image()));
            return true;
        }
    }

    // Fallback virtual handle
    frame.vkResourceHandle = reinterpret_cast<void*>(static_cast<uintptr_t>(0xC000 + frame.sequenceNumber));
    return true;
}

bool AVSyncPipeline::get_synchronized_pair(Frame& outVideo, AudioBuffer& outAudio, int64_t maxAllowedSkewNs) {
    if (impl_->videoQueue.empty() || impl_->audioQueue.empty()) {
        return false;
    }

    while (!impl_->videoQueue.empty() && !impl_->audioQueue.empty()) {
        Frame v{};
        AudioBuffer a{};

        if (!impl_->videoQueue.pop(v)) return false;
        if (!impl_->audioQueue.pop(a)) {
            // Put video back if audio couldn't be popped
            impl_->videoQueue.push(v);
            return false;
        }

        int64_t drift = static_cast<int64_t>(v.timestampNs) - static_cast<int64_t>(a.timestampNs);
        impl_->stats.currentDriftNs = drift;

        if (std::abs(drift) <= maxAllowedSkewNs) {
            outVideo = v;
            outAudio = a;
            impl_->stats.matchedAVPairs++;
            // Estimated latency: processing delta + 1 frame presentation buffer
            impl_->stats.estimatedGlassToGlassLatencyNs = static_cast<TimestampNs>(std::abs(drift)) + 8'333'333ULL;
            return true;
        }

        // Drop lagging frame to resynchronize
        if (drift > maxAllowedSkewNs) {
            // Audio is lagging behind video; drop audio and try next audio
            impl_->stats.droppedAudioBuffers++;
            impl_->videoQueue.push(v); // keep video
        } else {
            // Video is lagging behind audio; drop video and try next video
            impl_->stats.droppedVideoFrames++;
            impl_->audioQueue.push(a); // keep audio
        }
    }

    return false;
}

SyncStats AVSyncPipeline::stats() const noexcept {
    return impl_->stats;
}

size_t AVSyncPipeline::pending_video_frames() const noexcept {
    return impl_->videoQueue.size();
}

size_t AVSyncPipeline::pending_audio_buffers() const noexcept {
    return impl_->audioQueue.size();
}

} // namespace capapp::sync
