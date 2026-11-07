#pragma once

#include <capapp/common.hpp>
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace capapp {

enum class AudioSampleFormat : uint16_t {
    Unknown = 0,
    Float32,
    Int16,
    Int24,
    Int32
};

[[nodiscard]] constexpr std::string_view audio_format_name(AudioSampleFormat format) noexcept {
    switch (format) {
        case AudioSampleFormat::Float32: return "Float32";
        case AudioSampleFormat::Int16:   return "Int16";
        case AudioSampleFormat::Int24:   return "Int24";
        case AudioSampleFormat::Int32:   return "Int32";
        default: return "Unknown";
    }
}

/**
 * @brief High-precision audio buffer container.
 *
 * Adheres to repository guidelines:
 * - AudioBuffer* with timestamp in nanoseconds.
 * - Sized for WASAPI 2-4 frame low-latency pipeline.
 */
inline struct AudioBuffer {
    TimestampNs timestampNs{0};       // Nanosecond hardware capture/render timestamp
    uint32_t sampleRate{48000};       // Typical 48000Hz or 44100Hz
    uint16_t channels{2};             // Stereo default (2 channels)
    uint16_t bitsPerSample{32};       // 32-bit float or 16-bit PCM
    AudioSampleFormat format{AudioSampleFormat::Float32};
    uint32_t frameCount{0};           // Number of multi-channel audio frames
    uint8_t* data{nullptr};           // Pointer to raw PCM/IEEE float samples
    size_t dataSizeBytes{0};          // Total byte count
    bool isSilent{false};             // Flag indicating packet silence/padding

    [[nodiscard]] constexpr size_t bytes_per_frame() const noexcept {
        return static_cast<size_t>(channels) * (bitsPerSample / 8);
    }

    [[nodiscard]] constexpr TimestampNs duration_ns() const noexcept {
        if (sampleRate == 0) return 0;
        return (static_cast<TimestampNs>(frameCount) * 1'000'000'000ULL) / sampleRate;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return (sampleRate > 0) && (channels > 0) && (frameCount > 0) && (data != nullptr);
    }
} default_audio_buffer{};

} // namespace capapp
