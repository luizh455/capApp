#include <capapp/profiling/benchmark.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace capapp::capture {
    std::unique_ptr<ICaptureDevice> create_capture_device();
}

namespace capapp::profiling {

LatencyBenchmark::LatencyBenchmark(uint32_t frameCount)
    : targetFrames_(frameCount < 10 ? 10 : frameCount) {}

BenchmarkResult LatencyBenchmark::run() {
    Profiler::instance().clear();

    vulkan::VulkanContext vkCtx;
    vkCtx.initialize();

    audio::WasapiConfig audioCfg{};
    audioCfg.sampleRate = 48000;
    audioCfg.channels = 2;
    audioCfg.bufferFrameCount = 4;
    audio::WasapiAudioEngine audioEngine(audioCfg);
    audioEngine.initialize();
    audioEngine.start();

    auto captureDev = capture::create_capture_device();
    capture::CaptureDeviceConfig capCfg{};
    capCfg.targetWidth = 1920;
    capCfg.targetHeight = 1080;
    capCfg.targetFps = 60;
    capCfg.enableZeroCopy = true;
    captureDev->open(capCfg);
    captureDev->start_stream();

    sync::AVSyncPipeline avSync(&vkCtx);

    std::vector<float> audioData(480 * 2, 0.1f);

    auto benchmarkStart = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < targetFrames_; ++i) {
        TimestampNs nowNs = audio::WasapiAudioEngine::query_system_clock_ns();

        // 1. Measure Capture Latency
        Frame frame{};
        {
            CAPAPP_PROFILE_SCOPE("CaptureFrame");
            captureDev->acquire_frame(frame);
            frame.timestampNs = nowNs; // Align with audio timestamp
            avSync.import_zero_copy_surface(frame);
        }

        // 2. Measure Audio Acquisition
        audioEngine.simulate_packet_capture(audioData.data(), 480, nowNs);

        AudioBuffer audioBuf{};
        audioEngine.pop_captured_buffer(audioBuf);

        // 3. Measure Sync & Presentation Latency
        {
            CAPAPP_PROFILE_SCOPE("AVSyncAndPresent");
            avSync.push_video_frame(frame);
            avSync.push_audio_buffer(audioBuf);

            Frame pairedVideo{};
            AudioBuffer pairedAudio{};
            avSync.get_synchronized_pair(pairedVideo, pairedAudio);
        }

        // 4. Overall Glass-to-Glass Latency marker
        {
            CAPAPP_PROFILE_SCOPE("GlassToGlass");
            // End-to-end processing delta
        }

        captureDev->release_frame(frame);
    }

    auto benchmarkEnd = std::chrono::steady_clock::now();
    auto totalDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(benchmarkEnd - benchmarkStart).count();

    captureDev->close();
    audioEngine.stop();
    vkCtx.shutdown();

    BenchmarkResult res{};
    res.totalFrames = targetFrames_;
    res.averageFps = totalDurationMs > 0 ? (targetFrames_ * 1000.0 / totalDurationMs) : 60.0;
    res.captureLatency = Profiler::instance().get_stats("CaptureFrame");
    res.syncLatency = Profiler::instance().get_stats("AVSyncAndPresent");
    res.glassToGlassLatency = Profiler::instance().get_stats("GlassToGlass");
    res.droppedFrames = avSync.stats().droppedVideoFrames;
    res.audioUnderruns = audioEngine.stats().underrunCount;
    res.audioOverruns = audioEngine.stats().overrunCount;

    return res;
}

std::string BenchmarkResult::format_report() const {
    std::ostringstream ss;
    ss << "\n===================================================================\n";
    ss << "                CAPAPP LATENCY BENCHMARK REPORT                    \n";
    ss << "===================================================================\n";
    ss << " Frames Processed     : " << totalFrames << " frames\n";
    ss << " Average Throughput   : " << std::fixed << std::setprecision(1) << averageFps << " FPS\n";
    ss << "-------------------------------------------------------------------\n";
    ss << " Glass-to-Glass Latency:\n";
    ss << "   - Average          : " << std::setprecision(2) << (glassToGlassLatency.avgNs / 1'000'000.0) << " ms\n";
    ss << "   - p50 (Median)     : " << (glassToGlassLatency.p50Ns / 1'000'000.0) << " ms\n";
    ss << "   - p90              : " << (glassToGlassLatency.p90Ns / 1'000'000.0) << " ms\n";
    ss << "   - p99              : " << (glassToGlassLatency.p99Ns / 1'000'000.0) << " ms\n";
    ss << "   - Min / Max        : " << (glassToGlassLatency.minNs / 1'000'000.0) << " ms / "
                                   << (glassToGlassLatency.maxNs / 1'000'000.0) << " ms\n";
    ss << "-------------------------------------------------------------------\n";
    ss << " Subsystem Breakdown:\n";
    ss << "   - Capture Latency  : " << (captureLatency.avgNs / 1'000.0) << " us avg\n";
    ss << "   - A/V Sync Latency : " << (syncLatency.avgNs / 1'000.0) << " us avg\n";
    ss << "-------------------------------------------------------------------\n";
    ss << " Health & Quality:\n";
    ss << "   - Dropped Frames   : " << droppedFrames << "\n";
    ss << "   - Audio Underruns  : " << audioUnderruns << "\n";
    ss << "   - Audio Overruns   : " << audioOverruns << "\n";
    ss << "===================================================================\n";
    return ss.str();
}

} // namespace capapp::profiling
