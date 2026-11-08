#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace capapp::audio {

class AudioVolumeManager {
public:
    void set_volume(float volume) noexcept {
        volume_.store(std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 0.0f,
                      std::memory_order_relaxed);
    }

    [[nodiscard]] float volume() const noexcept {
        return volume_.load(std::memory_order_relaxed);
    }

    void set_muted(bool muted) noexcept {
        muted_.store(muted, std::memory_order_relaxed);
    }

    [[nodiscard]] bool is_muted() const noexcept {
        return muted_.load(std::memory_order_relaxed);
    }

    void apply(float* samples, size_t sampleCount) const noexcept {
        if (!samples) return;
        const float gain = is_muted() ? 0.0f : volume();
        for (size_t i = 0; i < sampleCount; ++i) {
            samples[i] *= gain;
        }
    }

private:
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> muted_{false};
};

} // namespace capapp::audio
