#pragma once

#include <capapp/profiling/profiler.hpp>
#include <capapp/vulkan/vulkan_context.hpp>
#include <capapp/audio/wasapi_engine.hpp>
#include <capapp/capture/capture_device.hpp>
#include <capapp/sync/av_sync_pipeline.hpp>
#include <string>

namespace capapp::profiling {

struct BenchmarkResult {
    uint64_t totalFrames{0};
    double averageFps{0.0};
    LatencyStats captureLatency;
    LatencyStats syncLatency;
    LatencyStats glassToGlassLatency;
    uint64_t droppedFrames{0};
    uint64_t audioUnderruns{0};
    uint64_t audioOverruns{0};

    [[nodiscard]] std::string format_report() const;
};

class LatencyBenchmark {
public:
    explicit LatencyBenchmark(uint32_t frameCount = 300);

    BenchmarkResult run();

private:
    uint32_t targetFrames_{300};
};

} // namespace capapp::profiling
