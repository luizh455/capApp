#include <gtest/gtest.h>
#include <capapp/profiling/profiler.hpp>
#include <capapp/profiling/benchmark.hpp>
#include <thread>
#include <chrono>

using namespace capapp;
using namespace capapp::profiling;

// Naming: {Device}::{Decoder}::When{Condition}
TEST(CaptureProfile_Timer, WhenMeasuredHighResolutionLatency) {
    auto& profiler = Profiler::instance();
    profiler.clear();

    {
        CAPAPP_PROFILE_SCOPE("TestTimerSection");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    LatencyStats stats = profiler.get_stats("TestTimerSection");
    EXPECT_EQ(stats.sampleCount, 1u);
    // 2ms is ~2,000,000 ns
    EXPECT_GT(stats.avgNs, 1'000'000ULL);
    EXPECT_GT(stats.p50Ns, 1'000'000ULL);
    EXPECT_EQ(stats.minNs, stats.maxNs);
}

TEST(CaptureProfile_RenderDoc, WhenSectionPushedAndPopped) {
    auto& profiler = Profiler::instance();
    profiler.set_renderdoc_attached(true);
    EXPECT_TRUE(profiler.is_renderdoc_attached());
    EXPECT_EQ(profiler.active_marker_count(), 0u);

    profiler.begin_renderdoc_marker("VulkanDrawPass");
    EXPECT_EQ(profiler.active_marker_count(), 1u);

    profiler.begin_renderdoc_marker("CompositeQuad");
    EXPECT_EQ(profiler.active_marker_count(), 2u);

    profiler.end_renderdoc_marker();
    EXPECT_EQ(profiler.active_marker_count(), 1u);

    profiler.end_renderdoc_marker();
    EXPECT_EQ(profiler.active_marker_count(), 0u);

    profiler.set_renderdoc_attached(false);
}

TEST(CaptureProfile_Benchmark, WhenSimulatedTargetFrames) {
    LatencyBenchmark bench(50);
    BenchmarkResult res = bench.run();

    EXPECT_EQ(res.totalFrames, 50u);
    EXPECT_GT(res.averageFps, 0.0);
    EXPECT_EQ(res.droppedFrames, 0u);
    EXPECT_EQ(res.audioUnderruns, 0u);
    EXPECT_EQ(res.audioOverruns, 0u);

    std::string report = res.format_report();
    EXPECT_FALSE(report.empty());
    EXPECT_NE(report.find("CAPAPP LATENCY BENCHMARK REPORT"), std::string::npos);
}
