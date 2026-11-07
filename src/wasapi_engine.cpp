#include <capapp/audio/wasapi_engine.hpp>
#include <vector>
#include <cstring>
#include <chrono>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace capapp::audio {

struct WasapiAudioEngine::Impl {
    WasapiConfig config;
    std::atomic<bool> running{false};
    RingBuffer<AudioBuffer> captureQueue{128};
    RingBuffer<AudioBuffer> renderQueue{128};
    std::vector<std::vector<uint8_t>> storagePool;
    size_t storageIndex{0};

    std::atomic<uint64_t> totalCaptured{0};
    std::atomic<uint64_t> totalRendered{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<TimestampNs> lastHwTimestamp{0};

    explicit Impl(WasapiConfig cfg) : config(cfg) {
        // Pre-allocate storage buffers to avoid dynamic allocation in hot audio path
        storagePool.resize(256);
        for (auto& buf : storagePool) {
            buf.resize(4096 * sizeof(float) * 2);
        }
    }

    uint8_t* acquire_storage(size_t requiredBytes) {
        if (storagePool.empty()) return nullptr;
        storageIndex = (storageIndex + 1) % storagePool.size();
        if (storagePool[storageIndex].size() < requiredBytes) {
            storagePool[storageIndex].resize(requiredBytes);
        }
        return storagePool[storageIndex].data();
    }
};

WasapiAudioEngine::WasapiAudioEngine(WasapiConfig config)
    : impl_(std::make_unique<Impl>(config)) {
    // Clamp buffer frame count to 2-4 frames max per repository guidelines
    if (impl_->config.bufferFrameCount < 2) {
        impl_->config.bufferFrameCount = 2;
    } else if (impl_->config.bufferFrameCount > 4) {
        impl_->config.bufferFrameCount = 4;
    }
}

WasapiAudioEngine::~WasapiAudioEngine() {
    stop();
}

bool WasapiAudioEngine::initialize() {
    return true;
}

bool WasapiAudioEngine::start() {
    impl_->running.store(true, std::memory_order_release);
    return true;
}

void WasapiAudioEngine::stop() {
    impl_->running.store(false, std::memory_order_release);
}

bool WasapiAudioEngine::is_running() const noexcept {
    return impl_->running.load(std::memory_order_acquire);
}

bool WasapiAudioEngine::pop_captured_buffer(AudioBuffer& outBuffer) {
    bool ok = impl_->captureQueue.pop(outBuffer);
    if (!ok) {
        impl_->underruns.fetch_add(1, std::memory_order_relaxed);
    }
    return ok;
}

bool WasapiAudioEngine::push_render_buffer(const AudioBuffer& inBuffer) {
    if (!impl_->renderQueue.push(inBuffer)) {
        impl_->overruns.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    impl_->totalRendered.fetch_add(inBuffer.frameCount, std::memory_order_relaxed);
    return true;
}

bool WasapiAudioEngine::simulate_packet_capture(const float* samples, uint32_t frameCount, TimestampNs hwTimestampNs) {
    if (!impl_->running.load(std::memory_order_relaxed)) {
        return false;
    }

    const size_t byteSize = static_cast<size_t>(frameCount) * impl_->config.channels * sizeof(float);
    uint8_t* storage = impl_->acquire_storage(byteSize);
    if (!storage) {
        impl_->overruns.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    std::memcpy(storage, samples, byteSize);

    AudioBuffer buf{};
    buf.timestampNs = hwTimestampNs;
    buf.sampleRate = impl_->config.sampleRate;
    buf.channels = impl_->config.channels;
    buf.bitsPerSample = impl_->config.bitsPerSample;
    buf.format = impl_->config.format;
    buf.frameCount = frameCount;
    buf.data = storage;
    buf.dataSizeBytes = byteSize;
    buf.isSilent = false;

    if (!impl_->captureQueue.push(buf)) {
        impl_->overruns.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    impl_->totalCaptured.fetch_add(frameCount, std::memory_order_relaxed);
    impl_->lastHwTimestamp.store(hwTimestampNs, std::memory_order_release);
    return true;
}

const WasapiConfig& WasapiAudioEngine::config() const noexcept {
    return impl_->config;
}

AudioStats WasapiAudioEngine::stats() const noexcept {
    AudioStats s{};
    s.totalFramesCaptured = impl_->totalCaptured.load(std::memory_order_relaxed);
    s.totalFramesRendered = impl_->totalRendered.load(std::memory_order_relaxed);
    s.overrunCount = impl_->overruns.load(std::memory_order_relaxed);
    s.underrunCount = impl_->underruns.load(std::memory_order_relaxed);
    s.lastHardwareTimestampNs = impl_->lastHwTimestamp.load(std::memory_order_relaxed);
    return s;
}

uint32_t WasapiAudioEngine::buffer_frame_count() const noexcept {
    return impl_->config.bufferFrameCount;
}

TimestampNs WasapiAudioEngine::query_system_clock_ns() noexcept {
#if defined(_WIN32)
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (static_cast<TimestampNs>(counter.QuadPart) * 1'000'000'000ULL) / freq.QuadPart;
#else
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
#endif
}

} // namespace capapp::audio
