#pragma once

#include <capapp/audio_buffer.hpp>
#include <capapp/ring_buffer.hpp>
#include <memory>
#include <string>
#include <atomic>
#include <cstdint>

namespace capapp::audio {

struct WasapiConfig {
    uint32_t sampleRate{48000};
    uint16_t channels{2};
    uint16_t bitsPerSample{32};
    AudioSampleFormat format{AudioSampleFormat::Float32};
    // Sized to 2-4 frames per repository guidelines
    uint32_t bufferFrameCount{4};
    bool exclusiveMode{true};
    bool eventDriven{true};
};

struct AudioStats {
    uint64_t totalFramesCaptured{0};
    uint64_t totalFramesRendered{0};
    uint64_t overrunCount{0};
    uint64_t underrunCount{0};
    TimestampNs lastHardwareTimestampNs{0};
};

struct AudioMeterLevels {
    float peakDbLeft{-60.0f};
    float peakDbRight{-60.0f};
    float rmsDbLeft{-60.0f};
    float rmsDbRight{-60.0f};
    float peakHoldLeft{-60.0f};
    float peakHoldRight{-60.0f};
};

class WasapiAudioEngine {
public:
    explicit WasapiAudioEngine(WasapiConfig config = {});
    ~WasapiAudioEngine();

    // Non-copyable
    WasapiAudioEngine(const WasapiAudioEngine&) = delete;
    WasapiAudioEngine& operator=(const WasapiAudioEngine&) = delete;

    /**
     * @brief Initializes WASAPI audio client in exclusive low-latency event-driven mode.
     */
    bool initialize();

    /**
     * @brief Starts audio capture/render streaming loop.
     */
    bool start();

    /**
     * @brief Stops audio streaming.
     */
    void stop();

    [[nodiscard]] bool is_running() const noexcept;

    /**
     * @brief Polls the latest AudioBuffer from the low-latency lock-free queue.
     */
    bool pop_captured_buffer(AudioBuffer& outBuffer);

    /**
     * @brief Pushes an AudioBuffer to the render queue for loopback/monitoring.
     */
    bool push_render_buffer(const AudioBuffer& inBuffer);

    /**
     * @brief Simulates an incoming audio packet for testing clock sync and buffer overruns.
     */
    bool simulate_packet_capture(const float* samples, uint32_t frameCount, TimestampNs hwTimestampNs);

    // Queries
    [[nodiscard]] const WasapiConfig& config() const noexcept;
    [[nodiscard]] AudioStats stats() const noexcept;
    [[nodiscard]] AudioMeterLevels meter_levels() const noexcept;
    void reset_meter() noexcept;
    [[nodiscard]] uint32_t buffer_frame_count() const noexcept;
    [[nodiscard]] static TimestampNs query_system_clock_ns() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace capapp::audio
