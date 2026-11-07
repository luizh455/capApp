#include <gtest/gtest.h>
#include <capapp/audio/wasapi_engine.hpp>
#include <vector>

using namespace capapp;
using namespace capapp::audio;

// Naming convention: {Device}::{Decoder}::When{Condition}
TEST(CaptureDevice_Audio, WhenInitialized_BufferIs2To4Frames) {
    // Attempting to set buffer frame count outside [2, 4] must be clamped
    WasapiConfig largeConfig{};
    largeConfig.bufferFrameCount = 32;
    WasapiAudioEngine largeEngine(largeConfig);
    EXPECT_LE(largeEngine.buffer_frame_count(), 4u);
    EXPECT_GE(largeEngine.buffer_frame_count(), 2u);

    WasapiConfig smallConfig{};
    smallConfig.bufferFrameCount = 1;
    WasapiAudioEngine smallEngine(smallConfig);
    EXPECT_GE(smallEngine.buffer_frame_count(), 2u);
    EXPECT_LE(smallEngine.buffer_frame_count(), 4u);

    WasapiConfig normalConfig{};
    normalConfig.bufferFrameCount = 3;
    WasapiAudioEngine normalEngine(normalConfig);
    EXPECT_EQ(normalEngine.buffer_frame_count(), 3u);
}

TEST(CaptureDevice_Audio, WhenAudioCaptured_TimestampsAreInNanoseconds) {
    WasapiAudioEngine engine;
    ASSERT_TRUE(engine.initialize());
    ASSERT_TRUE(engine.start());
    EXPECT_TRUE(engine.is_running());

    TimestampNs nowNs = WasapiAudioEngine::query_system_clock_ns();
    EXPECT_GT(nowNs, 0u);

    // Simulate 480 audio samples (10ms of 48kHz stereo)
    std::vector<float> samples(480 * 2, 0.5f);
    EXPECT_TRUE(engine.simulate_packet_capture(samples.data(), 480, nowNs));

    AudioBuffer buf{};
    EXPECT_TRUE(engine.pop_captured_buffer(buf));
    EXPECT_EQ(buf.timestampNs, nowNs);
    EXPECT_EQ(buf.frameCount, 480u);
    EXPECT_EQ(buf.sampleRate, 48000u);
    EXPECT_EQ(buf.channels, 2u);
    EXPECT_EQ(buf.duration_ns(), 10'000'000ULL);

    AudioStats stats = engine.stats();
    EXPECT_EQ(stats.totalFramesCaptured, 480u);
    EXPECT_EQ(stats.lastHardwareTimestampNs, nowNs);
    EXPECT_EQ(stats.overrunCount, 0u);
}

TEST(CaptureDevice_Audio, WhenOverrunOccurs_DropsFramesWithoutCrashing) {
    WasapiAudioEngine engine;
    ASSERT_TRUE(engine.initialize());
    ASSERT_TRUE(engine.start());

    std::vector<float> samples(480 * 2, 0.1f);
    TimestampNs baseTs = 1'000'000'000;

    // Fill and exceed ring buffer capacity
    for (size_t i = 0; i < 200; ++i) {
        engine.simulate_packet_capture(samples.data(), 480, baseTs + (i * 10'000'000));
    }

    AudioStats stats = engine.stats();
    EXPECT_GT(stats.overrunCount, 0u);
    EXPECT_GT(stats.totalFramesCaptured, 0u);

    // Drain buffer safely
    AudioBuffer drained{};
    size_t drainedCount = 0;
    while (engine.pop_captured_buffer(drained)) {
        ++drainedCount;
    }
    EXPECT_GT(drainedCount, 0u);
}

TEST(CaptureDevice_Audio, WhenRenderQueuePushed) {
    WasapiAudioEngine engine;
    ASSERT_TRUE(engine.initialize());
    ASSERT_TRUE(engine.start());

    AudioBuffer renderBuf{};
    renderBuf.timestampNs = 500'000'000;
    renderBuf.frameCount = 240;
    renderBuf.channels = 2;
    renderBuf.sampleRate = 48000;

    EXPECT_TRUE(engine.push_render_buffer(renderBuf));
    AudioStats stats = engine.stats();
    EXPECT_EQ(stats.totalFramesRendered, 240u);
}
