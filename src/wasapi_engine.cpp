#include <capapp/audio/wasapi_engine.hpp>
#include <vector>
#include <cstring>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <thread>
#include <cstdio>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#endif

namespace capapp::audio {

static float sample_to_db(float linear) noexcept {
    if (linear <= 1e-6f) return -60.0f;
    float db = 20.0f * std::log10(linear);
    return std::max(-60.0f, std::min(0.0f, db));
}

struct WasapiAudioEngine::Impl {
    WasapiConfig config;
    std::atomic<bool> running{false};
    std::atomic<bool> deviceCaptureAvailable{false};
    std::atomic<bool> monitoringAvailable{false};
    RingBuffer<AudioBuffer> captureQueue{128};
    RingBuffer<AudioBuffer> renderQueue{128};
    std::vector<std::vector<uint8_t>> storagePool;
    size_t storageIndex{0};
    AudioVolumeManager volumeManager;
    mutable std::mutex meterMutex;
    mutable std::mutex errorMutex;
    mutable std::mutex monitorErrorMutex;
    mutable std::mutex devicesMutex;
    std::vector<AudioInputDevice> inputDevices;
    std::vector<AudioInputDevice> outputDevices;
    std::wstring selectedDeviceId;
    std::wstring selectedDeviceName;
    std::wstring selectedOutputDeviceId;
    std::wstring selectedOutputDeviceName;
    std::string captureError;
    std::string monitorError;

    std::atomic<uint64_t> totalCaptured{0};
    std::atomic<uint64_t> totalRendered{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<TimestampNs> lastHwTimestamp{0};

    AudioMeterLevels meterLevels{};
#if defined(_WIN32)
    std::thread captureThread;
#endif

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

    bool submit_packet(const float* samples, uint32_t frameCount, TimestampNs timestamp,
                       bool silent = false, bool samplesAlreadyGained = false) {
        if (!running.load(std::memory_order_relaxed) || frameCount == 0) return false;
        const size_t sampleCount = static_cast<size_t>(frameCount) * config.channels;
        const size_t byteSize = sampleCount * sizeof(float);
        uint8_t* storage = acquire_storage(byteSize);
        if (!storage) {
            overruns.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        auto* output = reinterpret_cast<float*>(storage);
        if (silent || !samples) {
            std::fill_n(output, sampleCount, 0.0f);
        } else {
            std::memcpy(output, samples, byteSize);
        }
        if (!samplesAlreadyGained) volumeManager.apply(output, sampleCount);

        float maxLeft = 0.0f;
        float maxRight = 0.0f;
        double sumSquareLeft = 0.0;
        double sumSquareRight = 0.0;
        if (config.channels >= 2) {
            for (uint32_t i = 0; i < frameCount; ++i) {
                const float left = std::abs(output[i * config.channels]);
                const float right = std::abs(output[i * config.channels + 1]);
                maxLeft = std::max(maxLeft, left);
                maxRight = std::max(maxRight, right);
                sumSquareLeft += static_cast<double>(left) * left;
                sumSquareRight += static_cast<double>(right) * right;
            }
        } else {
            for (uint32_t i = 0; i < frameCount; ++i) {
                const float mono = std::abs(output[i]);
                maxLeft = std::max(maxLeft, mono);
                sumSquareLeft += static_cast<double>(mono) * mono;
            }
            maxRight = maxLeft;
            sumSquareRight = sumSquareLeft;
        }

        const float rmsLeft = std::sqrt(static_cast<float>(sumSquareLeft / frameCount));
        const float rmsRight = std::sqrt(static_cast<float>(sumSquareRight / frameCount));
        {
            std::lock_guard lock(meterMutex);
            meterLevels.peakDbLeft = sample_to_db(maxLeft);
            meterLevels.peakDbRight = sample_to_db(maxRight);
            meterLevels.rmsDbLeft = sample_to_db(rmsLeft);
            meterLevels.rmsDbRight = sample_to_db(rmsRight);
            meterLevels.peakHoldLeft = std::max(meterLevels.peakHoldLeft, meterLevels.peakDbLeft);
            meterLevels.peakHoldRight = std::max(meterLevels.peakHoldRight, meterLevels.peakDbRight);
        }

        AudioBuffer buffer{};
        buffer.timestampNs = timestamp;
        buffer.sampleRate = config.sampleRate;
        buffer.channels = config.channels;
        buffer.bitsPerSample = 32;
        buffer.format = AudioSampleFormat::Float32;
        buffer.frameCount = frameCount;
        buffer.data = storage;
        buffer.dataSizeBytes = byteSize;
        buffer.isSilent = silent;
        if (!captureQueue.push(buffer)) {
            overruns.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        totalCaptured.fetch_add(frameCount, std::memory_order_relaxed);
        lastHwTimestamp.store(timestamp, std::memory_order_release);
        return true;
    }

#if defined(_WIN32)
    void set_capture_error(const char* message, HRESULT result = S_OK) {
        std::lock_guard lock(errorMutex);
        captureError = message;
        if (FAILED(result)) {
            captureError += " (HRESULT 0x";
            char code[9]{};
            std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(result));
            captureError += code;
            captureError += ")";
        }
    }

    void set_monitor_error(const char* message, HRESULT result = S_OK) {
        std::lock_guard lock(monitorErrorMutex);
        monitorError = message;
        if (FAILED(result)) {
            monitorError += " (HRESULT 0x";
            char code[9]{};
            std::snprintf(code, sizeof(code), "%08lX", static_cast<unsigned long>(result));
            monitorError += code;
            monitorError += ")";
        }
    }

    void capture_loop() {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult)) {
            set_capture_error("Could not initialize COM for audio capture", comResult);
            return;
        }

        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioClient* client = nullptr;
        IAudioCaptureClient* capture = nullptr;
        WAVEFORMATEX* mixFormat = nullptr;
        HANDLE eventHandle = nullptr;
        IMMDevice* outputDevice = nullptr;
        IAudioClient* outputClient = nullptr;
        IAudioRenderClient* render = nullptr;
        WAVEFORMATEX* outputFormat = nullptr;
        HANDLE outputEvent = nullptr;
        UINT32 outputBufferFrameCount = 0;
        std::wstring selectedId;
        std::wstring selectedOutputId;
        {
            std::lock_guard lock(devicesMutex);
            selectedId = selectedDeviceId;
            selectedOutputId = selectedOutputDeviceId;
        }
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
        if (SUCCEEDED(result)) {
            result = selectedId.empty()
                ? enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device)
                : enumerator->GetDevice(selectedId.c_str(), &device);
        }
        if (SUCCEEDED(result)) result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(&client));
        if (SUCCEEDED(result)) result = client->GetMixFormat(&mixFormat);
        if (SUCCEEDED(result)) {
            result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 10'000'000, 0, mixFormat, nullptr);
        }
        if (SUCCEEDED(result)) result = client->GetService(__uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(&capture));
        if (SUCCEEDED(result)) {
            eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!eventHandle) result = HRESULT_FROM_WIN32(GetLastError());
        }
        if (SUCCEEDED(result)) result = client->SetEventHandle(eventHandle);

        HRESULT outputResult = selectedOutputId.empty()
            ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &outputDevice)
            : enumerator->GetDevice(selectedOutputId.c_str(), &outputDevice);
        if (SUCCEEDED(outputResult)) {
            outputResult = outputDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(&outputClient));
        }
        if (SUCCEEDED(outputResult)) outputResult = outputClient->GetMixFormat(&outputFormat);
        if (SUCCEEDED(outputResult)) {
            outputResult = outputClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 10'000'000, 0, outputFormat, nullptr);
        }
        if (SUCCEEDED(outputResult)) outputResult = outputClient->GetBufferSize(&outputBufferFrameCount);
        if (SUCCEEDED(outputResult)) {
            outputResult = outputClient->GetService(__uuidof(IAudioRenderClient),
                reinterpret_cast<void**>(&render));
        }
        if (SUCCEEDED(outputResult)) {
            outputEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!outputEvent) outputResult = HRESULT_FROM_WIN32(GetLastError());
        }
        if (SUCCEEDED(outputResult)) outputResult = outputClient->SetEventHandle(outputEvent);

        bool outputFloat = false;
        bool outputPcm = false;
        uint16_t outputValidBits = 0;
        if (SUCCEEDED(outputResult) && outputFormat) {
            outputFloat = outputFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            outputPcm = outputFormat->wFormatTag == WAVE_FORMAT_PCM;
            outputValidBits = outputFormat->wBitsPerSample;
            if (outputFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                outputFormat->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
                const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(outputFormat);
                outputFloat = IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
                outputPcm = IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
                outputValidBits = extensible->Samples.wValidBitsPerSample;
                if (outputValidBits == 0) outputValidBits = outputFormat->wBitsPerSample;
            }
            if (outputFormat->nChannels == 0 || outputFormat->nBlockAlign == 0 ||
                (!outputFloat && !outputPcm) ||
                (outputFloat && outputFormat->wBitsPerSample != 32 &&
                 outputFormat->wBitsPerSample != 64) ||
                (outputPcm && outputValidBits != 8 && outputValidBits != 16 &&
                 outputValidBits != 24 && outputValidBits != 32)) {
                outputResult = AUDCLNT_E_UNSUPPORTED_FORMAT;
            }
        }
        if (SUCCEEDED(outputResult)) outputResult = outputClient->Start();
        if (SUCCEEDED(outputResult)) {
            monitoringAvailable.store(true, std::memory_order_release);
            std::lock_guard lock(monitorErrorMutex);
            monitorError.clear();
        } else {
            set_monitor_error("Could not start audio monitoring on the selected output", outputResult);
        }

        bool isFloat = false;
        bool isPcm = false;
        uint16_t validBits = 0;
        if (SUCCEEDED(result) && mixFormat) {
            isFloat = mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            isPcm = mixFormat->wFormatTag == WAVE_FORMAT_PCM;
            validBits = mixFormat->wBitsPerSample;
            if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                mixFormat->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
                const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mixFormat);
                isFloat = IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
                isPcm = IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
                validBits = extensible->Samples.wValidBitsPerSample;
                if (validBits == 0) validBits = mixFormat->wBitsPerSample;
            } else if (mixFormat->wFormatTag == WAVE_FORMAT_PCM) {
                isFloat = false;
            } else if (!isFloat) {
                result = AUDCLNT_E_UNSUPPORTED_FORMAT;
            }
            const uint16_t channels = mixFormat->nChannels;
            if (channels == 0 || mixFormat->nBlockAlign == 0 ||
                (!isFloat && !isPcm) ||
                (isFloat && mixFormat->wBitsPerSample != 32 && mixFormat->wBitsPerSample != 64) ||
                (isPcm && validBits != 8 && validBits != 16 &&
                 validBits != 24 && validBits != 32)) {
                result = AUDCLNT_E_UNSUPPORTED_FORMAT;
            } else {
                config.sampleRate = mixFormat->nSamplesPerSec;
                config.channels = channels == 1 ? 1 : 2;
            }
        }
        if (SUCCEEDED(result)) result = client->Start();
        if (FAILED(result)) {
            set_capture_error("Could not start the selected Windows audio input", result);
        } else {
            {
                std::lock_guard lock(errorMutex);
                captureError.clear();
            }
            deviceCaptureAvailable.store(true, std::memory_order_release);
            std::vector<float> converted;
            bool failed = false;
            while (running.load(std::memory_order_acquire)) {
                const DWORD waitResult = WaitForSingleObject(eventHandle, 100);
                if (waitResult == WAIT_TIMEOUT) continue;
                if (waitResult != WAIT_OBJECT_0) {
                    set_capture_error("Wait for the Windows audio capture event failed",
                                      HRESULT_FROM_WIN32(GetLastError()));
                    failed = true;
                    break;
                }

                UINT32 packetFrames = 0;
                result = capture->GetNextPacketSize(&packetFrames);
                while (SUCCEEDED(result) && packetFrames > 0) {
                    BYTE* packet = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    UINT64 devicePosition = 0;
                    UINT64 qpcPosition = 0;
                    result = capture->GetBuffer(&packet, &frames, &flags,
                                                &devicePosition, &qpcPosition);
                    if (FAILED(result)) break;
                    const size_t sampleCount = static_cast<size_t>(frames) * config.channels;
                    converted.resize(sampleCount);
                    const uint16_t bytesPerSample = mixFormat->wBitsPerSample / 8;
                    const uint16_t sourceChannels = mixFormat->nChannels;
                    const bool packetSilent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    if (packetSilent) {
                        std::fill(converted.begin(), converted.end(), 0.0f);
                    } else {
                        for (UINT32 frame = 0; frame < frames; ++frame) {
                            for (uint16_t channel = 0; channel < config.channels; ++channel) {
                                const uint16_t sourceChannel = channel < sourceChannels ? channel : 0;
                                const BYTE* sample = packet +
                                    (static_cast<size_t>(frame) * sourceChannels + sourceChannel) * bytesPerSample;
                                float value = 0.0f;
                                if (isFloat && mixFormat->wBitsPerSample == 32) {
                                    std::memcpy(&value, sample, sizeof(value));
                                } else if (isFloat && mixFormat->wBitsPerSample == 64) {
                                    double sample64 = 0.0;
                                    std::memcpy(&sample64, sample, sizeof(sample64));
                                    value = static_cast<float>(sample64);
                                } else if (!isFloat && validBits == 8) {
                                    value = (static_cast<float>(*sample) - 128.0f) / 128.0f;
                                } else if (!isFloat && validBits == 16) {
                                    int16_t pcm = 0;
                                    std::memcpy(&pcm, sample, sizeof(pcm));
                                    value = static_cast<float>(pcm) / 32768.0f;
                                } else if (!isFloat && validBits == 24 && mixFormat->wBitsPerSample == 32) {
                                    int32_t pcm = 0;
                                    std::memcpy(&pcm, sample, sizeof(pcm));
                                    value = static_cast<float>(pcm >> 8) / 8388608.0f;
                                } else if (!isFloat && validBits == 24) {
                                    int32_t pcm = static_cast<int32_t>(sample[0]) |
                                        (static_cast<int32_t>(sample[1]) << 8) |
                                        (static_cast<int32_t>(sample[2]) << 16);
                                    if ((pcm & 0x800000) != 0) pcm |= static_cast<int32_t>(0xFF000000);
                                    value = static_cast<float>(pcm) / 8388608.0f;
                                } else if (!isFloat && validBits == 32) {
                                    int32_t pcm = 0;
                                    std::memcpy(&pcm, sample, sizeof(pcm));
                                    value = static_cast<float>(pcm) / 2147483648.0f;
                                }
                                converted[static_cast<size_t>(frame) * config.channels + channel] = value;
                            }
                        }
                    }
                    const TimestampNs timestamp = qpcPosition
                        ? static_cast<TimestampNs>(qpcPosition) * 100
                        : WasapiAudioEngine::query_system_clock_ns();
                    volumeManager.apply(converted.data(), converted.size());
                    submit_packet(converted.data(), frames, timestamp, packetSilent, true);

                    if (monitoringAvailable.load(std::memory_order_acquire) && frames > 0) {
                        const uint64_t scaledFrames =
                            (static_cast<uint64_t>(frames) * outputFormat->nSamplesPerSec +
                             config.sampleRate / 2) / config.sampleRate;
                        const UINT32 outputFrames = static_cast<UINT32>(
                            std::max<uint64_t>(1, scaledFrames));
                        UINT32 outputOffset = 0;
                        while (outputOffset < outputFrames && running.load(std::memory_order_acquire)) {
                            UINT32 padding = 0;
                            HRESULT renderResult = outputClient->GetCurrentPadding(&padding);
                            if (FAILED(renderResult)) {
                                set_monitor_error("Could not query audio output buffer space", renderResult);
                                monitoringAvailable.store(false, std::memory_order_release);
                                break;
                            }
                            const UINT32 availableFrames = outputBufferFrameCount > padding
                                ? outputBufferFrameCount - padding : 0;
                            if (availableFrames == 0) {
                                const DWORD wait = WaitForSingleObject(outputEvent, 100);
                                if (wait != WAIT_OBJECT_0 && wait != WAIT_TIMEOUT) {
                                    set_monitor_error("Waiting for audio output buffer space failed",
                                        HRESULT_FROM_WIN32(GetLastError()));
                                    monitoringAvailable.store(false, std::memory_order_release);
                                    break;
                                }
                                continue;
                            }

                            const UINT32 framesToWrite = std::min(outputFrames - outputOffset, availableFrames);
                            BYTE* outputData = nullptr;
                            renderResult = render->GetBuffer(framesToWrite, &outputData);
                            if (FAILED(renderResult)) {
                                set_monitor_error("Could not acquire audio output buffer", renderResult);
                                monitoringAvailable.store(false, std::memory_order_release);
                                break;
                            }
                            const uint16_t outputChannels = outputFormat->nChannels;
                            const uint16_t outputBytesPerSample = outputFormat->wBitsPerSample / 8;
                            for (UINT32 frame = 0; frame < framesToWrite; ++frame) {
                                const uint32_t destinationFrame = outputOffset + frame;
                                const double sourcePosition =
                                    static_cast<double>(destinationFrame) * config.sampleRate /
                                    outputFormat->nSamplesPerSec;
                                const uint32_t sourceFrame = std::min<uint32_t>(
                                    static_cast<uint32_t>(sourcePosition), frames - 1);
                                const uint32_t nextSourceFrame = std::min(sourceFrame + 1, frames - 1);
                                const float fraction = static_cast<float>(sourcePosition - sourceFrame);
                                for (uint16_t channel = 0; channel < outputChannels; ++channel) {
                                    float sampleValue = 0.0f;
                                    if (outputChannels == 1 && config.channels >= 2) {
                                        const float first =
                                            (converted[sourceFrame * config.channels] +
                                             converted[sourceFrame * config.channels + 1]) * 0.5f;
                                        const float second =
                                            (converted[nextSourceFrame * config.channels] +
                                             converted[nextSourceFrame * config.channels + 1]) * 0.5f;
                                        sampleValue = first + (second - first) * fraction;
                                    } else if (channel < 2 && config.channels == 1) {
                                        const float first = converted[sourceFrame];
                                        const float second = converted[nextSourceFrame];
                                        sampleValue = first + (second - first) * fraction;
                                    } else if (channel < config.channels) {
                                        const float first = converted[sourceFrame * config.channels + channel];
                                        const float second = converted[nextSourceFrame * config.channels + channel];
                                        sampleValue = first + (second - first) * fraction;
                                    }
                                    BYTE* sample = outputData +
                                        static_cast<size_t>(frame) * outputFormat->nBlockAlign +
                                        static_cast<size_t>(channel) * outputBytesPerSample;
                                    sampleValue = std::clamp(sampleValue, -1.0f, 1.0f);
                                    if (outputFloat && outputFormat->wBitsPerSample == 32) {
                                        std::memcpy(sample, &sampleValue, sizeof(sampleValue));
                                    } else if (outputFloat) {
                                        const double sample64 = sampleValue;
                                        std::memcpy(sample, &sample64, sizeof(sample64));
                                    } else if (outputValidBits == 8) {
                                        *sample = static_cast<BYTE>(
                                            std::clamp(sampleValue * 128.0f + 128.0f, 0.0f, 255.0f));
                                    } else if (outputValidBits == 16) {
                                        const int16_t pcm = static_cast<int16_t>(
                                            std::clamp(sampleValue * 32768.0f, -32768.0f, 32767.0f));
                                        std::memcpy(sample, &pcm, sizeof(pcm));
                                    } else if (outputValidBits == 24 && outputFormat->wBitsPerSample == 32) {
                                        const int32_t pcm = static_cast<int32_t>(
                                            std::clamp(sampleValue * 8388607.0f, -8388608.0f, 8388607.0f));
                                        const int32_t alignedPcm = pcm * 256;
                                        std::memcpy(sample, &alignedPcm, sizeof(alignedPcm));
                                    } else if (outputValidBits == 24) {
                                        const int32_t pcm = static_cast<int32_t>(
                                            std::clamp(sampleValue * 8388607.0f, -8388608.0f, 8388607.0f));
                                        sample[0] = static_cast<BYTE>(pcm);
                                        sample[1] = static_cast<BYTE>(pcm >> 8);
                                        sample[2] = static_cast<BYTE>(pcm >> 16);
                                    } else {
                                        const double scaled = std::clamp(
                                            static_cast<double>(sampleValue) * 2147483648.0,
                                            -2147483648.0, 2147483647.0);
                                        const int32_t pcm = static_cast<int32_t>(scaled);
                                        std::memcpy(sample, &pcm, sizeof(pcm));
                                    }
                                }
                            }
                            renderResult = render->ReleaseBuffer(framesToWrite, 0);
                            if (FAILED(renderResult)) {
                                set_monitor_error("Could not submit audio to output device", renderResult);
                                monitoringAvailable.store(false, std::memory_order_release);
                                break;
                            }
                            outputOffset += framesToWrite;
                            totalRendered.fetch_add(framesToWrite, std::memory_order_relaxed);
                        }
                    }
                    result = capture->ReleaseBuffer(frames);
                    if (FAILED(result)) break;
                    result = capture->GetNextPacketSize(&packetFrames);
                }
                if (FAILED(result)) {
                    set_capture_error("Reading a Windows audio capture packet failed", result);
                    failed = true;
                    break;
                }
            }
            if (failed) deviceCaptureAvailable.store(false, std::memory_order_release);
        }

        if (client) client->Stop();
        if (outputClient) outputClient->Stop();
        if (eventHandle) CloseHandle(eventHandle);
        if (outputEvent) CloseHandle(outputEvent);
        if (mixFormat) CoTaskMemFree(mixFormat);
        if (outputFormat) CoTaskMemFree(outputFormat);
        if (capture) capture->Release();
        if (render) render->Release();
        if (outputClient) outputClient->Release();
        if (outputDevice) outputDevice->Release();
        if (client) client->Release();
        if (device) device->Release();
        if (enumerator) enumerator->Release();
        CoUninitialize();
    }
#endif
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
    refresh_input_devices();
    refresh_output_devices();
    return true;
}

bool WasapiAudioEngine::start() {
    bool expected = false;
    if (!impl_->running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }
#if defined(_WIN32)
    if (impl_->config.enableDeviceCapture) {
        try {
            impl_->captureThread = std::thread(&Impl::capture_loop, impl_.get());
        } catch (const std::system_error& error) {
            impl_->running.store(false, std::memory_order_release);
            std::lock_guard lock(impl_->errorMutex);
            impl_->captureError = std::string("Could not create audio capture thread: ") + error.what();
            return false;
        }
    }
#endif
    return true;
}

void WasapiAudioEngine::stop() {
    impl_->running.store(false, std::memory_order_release);
    impl_->deviceCaptureAvailable.store(false, std::memory_order_release);
    impl_->monitoringAvailable.store(false, std::memory_order_release);
#if defined(_WIN32)
    if (impl_->captureThread.joinable()) impl_->captureThread.join();
#endif
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
    if (!samples || impl_->config.format != AudioSampleFormat::Float32) return false;
    return impl_->submit_packet(samples, frameCount, hwTimestampNs);
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

AudioMeterLevels WasapiAudioEngine::meter_levels() const noexcept {
    std::lock_guard lock(impl_->meterMutex);
    return impl_->meterLevels;
}

void WasapiAudioEngine::reset_meter() noexcept {
    std::lock_guard lock(impl_->meterMutex);
    impl_->meterLevels = AudioMeterLevels{};
}

uint32_t WasapiAudioEngine::buffer_frame_count() const noexcept {
    return impl_->config.bufferFrameCount;
}

bool WasapiAudioEngine::device_capture_available() const noexcept {
    return impl_->deviceCaptureAvailable.load(std::memory_order_acquire);
}

std::string WasapiAudioEngine::capture_error() const {
    std::lock_guard lock(impl_->errorMutex);
    return impl_->captureError;
}

bool WasapiAudioEngine::monitoring_available() const noexcept {
    return impl_->monitoringAvailable.load(std::memory_order_acquire);
}

std::string WasapiAudioEngine::monitoring_error() const {
    std::lock_guard lock(impl_->monitorErrorMutex);
    return impl_->monitorError;
}

bool WasapiAudioEngine::refresh_input_devices() {
#if defined(_WIN32)
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        impl_->set_capture_error("Could not initialize COM to enumerate audio inputs", comResult);
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    IMMDevice* defaultDevice = nullptr;
    LPWSTR defaultId = nullptr;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(result)) {
        result = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
    }
    if (SUCCEEDED(result) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &defaultDevice))) {
        defaultDevice->GetId(&defaultId);
    }

    std::vector<AudioInputDevice> devices;
    UINT count = 0;
    if (SUCCEEDED(result)) result = collection->GetCount(&count);
    for (UINT i = 0; SUCCEEDED(result) && i < count; ++i) {
        IMMDevice* device = nullptr;
        IPropertyStore* properties = nullptr;
        LPWSTR id = nullptr;
        PROPVARIANT friendlyName;
        PropVariantInit(&friendlyName);

        result = collection->Item(i, &device);
        if (SUCCEEDED(result)) result = device->GetId(&id);
        if (SUCCEEDED(result)) result = device->OpenPropertyStore(STGM_READ, &properties);
        if (SUCCEEDED(result)) result = properties->GetValue(PKEY_Device_FriendlyName, &friendlyName);
        if (SUCCEEDED(result) && id) {
            AudioInputDevice entry;
            entry.id = id;
            entry.name = friendlyName.vt == VT_LPWSTR && friendlyName.pwszVal
                ? friendlyName.pwszVal : id;
            devices.push_back(std::move(entry));
        }

        PropVariantClear(&friendlyName);
        if (id) CoTaskMemFree(id);
        if (properties) properties->Release();
        if (device) device->Release();
    }

    const std::wstring defaultDeviceId = defaultId ? defaultId : L"";
    if (defaultId) CoTaskMemFree(defaultId);
    if (defaultDevice) defaultDevice->Release();
    if (collection) collection->Release();
    if (enumerator) enumerator->Release();
    if (uninitializeCom) CoUninitialize();

    if (FAILED(result)) {
        impl_->set_capture_error("Could not enumerate Windows audio input devices", result);
        return false;
    }

    {
        std::lock_guard lock(impl_->devicesMutex);
        impl_->inputDevices = std::move(devices);
        const auto selected = std::find_if(
            impl_->inputDevices.begin(), impl_->inputDevices.end(),
            [this](const AudioInputDevice& device) {
                return impl_->selectedDeviceId == device.id;
            });
        if (selected == impl_->inputDevices.end()) {
            const auto defaultDeviceIt = std::find_if(
                impl_->inputDevices.begin(), impl_->inputDevices.end(),
                [&defaultDeviceId](const AudioInputDevice& device) {
                    return !defaultDeviceId.empty() && device.id == defaultDeviceId;
                });
            const auto chosen = defaultDeviceIt != impl_->inputDevices.end()
                ? defaultDeviceIt : impl_->inputDevices.begin();
            impl_->selectedDeviceId = chosen == impl_->inputDevices.end() ? L"" : chosen->id;
            impl_->selectedDeviceName = chosen == impl_->inputDevices.end() ? L"" : chosen->name;
        } else {
            impl_->selectedDeviceName = selected->name;
        }
    }
    return true;
#else
    return true;
#endif
}

bool WasapiAudioEngine::refresh_output_devices() {
#if defined(_WIN32)
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        impl_->set_monitor_error("Could not initialize COM to enumerate audio outputs", comResult);
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    IMMDevice* defaultDevice = nullptr;
    LPWSTR defaultId = nullptr;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(result)) {
        result = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    }
    if (SUCCEEDED(result) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
        defaultDevice->GetId(&defaultId);
    }

    std::vector<AudioInputDevice> devices;
    UINT count = 0;
    if (SUCCEEDED(result)) result = collection->GetCount(&count);
    for (UINT i = 0; SUCCEEDED(result) && i < count; ++i) {
        IMMDevice* device = nullptr;
        IPropertyStore* properties = nullptr;
        LPWSTR id = nullptr;
        PROPVARIANT friendlyName;
        PropVariantInit(&friendlyName);

        result = collection->Item(i, &device);
        if (SUCCEEDED(result)) result = device->GetId(&id);
        if (SUCCEEDED(result)) result = device->OpenPropertyStore(STGM_READ, &properties);
        if (SUCCEEDED(result)) result = properties->GetValue(PKEY_Device_FriendlyName, &friendlyName);
        if (SUCCEEDED(result) && id) {
            AudioInputDevice entry;
            entry.id = id;
            entry.name = friendlyName.vt == VT_LPWSTR && friendlyName.pwszVal
                ? friendlyName.pwszVal : id;
            devices.push_back(std::move(entry));
        }

        PropVariantClear(&friendlyName);
        if (id) CoTaskMemFree(id);
        if (properties) properties->Release();
        if (device) device->Release();
    }

    const std::wstring defaultDeviceId = defaultId ? defaultId : L"";
    if (defaultId) CoTaskMemFree(defaultId);
    if (defaultDevice) defaultDevice->Release();
    if (collection) collection->Release();
    if (enumerator) enumerator->Release();
    if (uninitializeCom) CoUninitialize();

    if (FAILED(result)) {
        impl_->set_monitor_error("Could not enumerate Windows audio output devices", result);
        return false;
    }

    {
        std::lock_guard lock(impl_->devicesMutex);
        impl_->outputDevices = std::move(devices);
        const auto selected = std::find_if(
            impl_->outputDevices.begin(), impl_->outputDevices.end(),
            [this](const AudioInputDevice& device) {
                return impl_->selectedOutputDeviceId == device.id;
            });
        if (selected == impl_->outputDevices.end()) {
            const auto defaultDeviceIt = std::find_if(
                impl_->outputDevices.begin(), impl_->outputDevices.end(),
                [&defaultDeviceId](const AudioInputDevice& device) {
                    return !defaultDeviceId.empty() && device.id == defaultDeviceId;
                });
            const auto chosen = defaultDeviceIt != impl_->outputDevices.end()
                ? defaultDeviceIt : impl_->outputDevices.begin();
            impl_->selectedOutputDeviceId =
                chosen == impl_->outputDevices.end() ? L"" : chosen->id;
            impl_->selectedOutputDeviceName =
                chosen == impl_->outputDevices.end() ? L"" : chosen->name;
        } else {
            impl_->selectedOutputDeviceName = selected->name;
        }
    }
    return true;
#else
    return true;
#endif
}

std::vector<AudioInputDevice> WasapiAudioEngine::input_devices() const {
    std::lock_guard lock(impl_->devicesMutex);
    return impl_->inputDevices;
}

std::vector<AudioInputDevice> WasapiAudioEngine::output_devices() const {
    std::lock_guard lock(impl_->devicesMutex);
    return impl_->outputDevices;
}

std::wstring WasapiAudioEngine::selected_input_device_name() const {
    std::lock_guard lock(impl_->devicesMutex);
    return impl_->selectedDeviceName;
}

std::wstring WasapiAudioEngine::selected_input_device_id() const {
    std::lock_guard lock(impl_->devicesMutex);
    return impl_->selectedDeviceId;
}

std::wstring WasapiAudioEngine::selected_output_device_name() const {
    std::lock_guard lock(impl_->devicesMutex);
    return impl_->selectedOutputDeviceName;
}

std::wstring WasapiAudioEngine::selected_output_device_id() const {
    std::lock_guard lock(impl_->devicesMutex);
    return impl_->selectedOutputDeviceId;
}

bool WasapiAudioEngine::select_input_device_id(const std::wstring& id) {
    AudioInputDevice selected;
    {
        std::lock_guard lock(impl_->devicesMutex);
        const auto device = std::find_if(
            impl_->inputDevices.begin(), impl_->inputDevices.end(),
            [&id](const AudioInputDevice& input) { return input.id == id; });
        if (device == impl_->inputDevices.end()) return false;
        selected = *device;
    }

    const bool wasRunning = impl_->running.load(std::memory_order_acquire);
    if (wasRunning) stop();
    {
        std::lock_guard lock(impl_->devicesMutex);
        impl_->selectedDeviceId = std::move(selected.id);
        impl_->selectedDeviceName = std::move(selected.name);
    }
    AudioBuffer staleBuffer{};
    while (impl_->captureQueue.pop(staleBuffer)) {}
    return !wasRunning || start();
}

bool WasapiAudioEngine::select_output_device_id(const std::wstring& id) {
    AudioInputDevice selected;
    {
        std::lock_guard lock(impl_->devicesMutex);
        const auto device = std::find_if(
            impl_->outputDevices.begin(), impl_->outputDevices.end(),
            [&id](const AudioInputDevice& output) { return output.id == id; });
        if (device == impl_->outputDevices.end()) return false;
        selected = *device;
    }

    const bool wasRunning = impl_->running.load(std::memory_order_acquire);
    if (wasRunning) stop();
    {
        std::lock_guard lock(impl_->devicesMutex);
        impl_->selectedOutputDeviceId = std::move(selected.id);
        impl_->selectedOutputDeviceName = std::move(selected.name);
    }
    return !wasRunning || start();
}

bool WasapiAudioEngine::cycle_input_device(int direction) {
    AudioInputDevice next;
    {
        std::lock_guard lock(impl_->devicesMutex);
        if (impl_->inputDevices.size() < 2) return false;
        const auto current = std::find_if(
            impl_->inputDevices.begin(), impl_->inputDevices.end(),
            [this](const AudioInputDevice& device) {
                return device.id == impl_->selectedDeviceId;
            });
        const size_t count = impl_->inputDevices.size();
        const size_t currentIndex = current == impl_->inputDevices.end()
            ? (direction < 0 ? 0 : count - 1)
            : static_cast<size_t>(current - impl_->inputDevices.begin());
        const size_t nextIndex = direction < 0
            ? (currentIndex + count - 1) % count
            : (currentIndex + 1) % count;
        next = impl_->inputDevices[nextIndex];
    }

    const bool wasRunning = impl_->running.load(std::memory_order_acquire);
    if (wasRunning) stop();
    {
        std::lock_guard lock(impl_->devicesMutex);
        impl_->selectedDeviceId = std::move(next.id);
        impl_->selectedDeviceName = std::move(next.name);
    }

    AudioBuffer staleBuffer{};
    while (impl_->captureQueue.pop(staleBuffer)) {}

    return !wasRunning || start();
}

void WasapiAudioEngine::set_volume(float volume) noexcept {
    impl_->volumeManager.set_volume(volume);
}

float WasapiAudioEngine::volume() const noexcept {
    return impl_->volumeManager.volume();
}

void WasapiAudioEngine::set_muted(bool muted) noexcept {
    impl_->volumeManager.set_muted(muted);
}

bool WasapiAudioEngine::is_muted() const noexcept {
    return impl_->volumeManager.is_muted();
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
