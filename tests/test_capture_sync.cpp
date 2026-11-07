#include <gtest/gtest.h>
#include <capapp/capture/capture_device.hpp>
#include <capapp/decoder/decoder.hpp>
#include <capapp/sync/av_sync_pipeline.hpp>
#include <capapp/vulkan/vulkan_context.hpp>

using namespace capapp;
using namespace capapp::capture;
using namespace capapp::decoder;
using namespace capapp::sync;
using namespace capapp::vulkan;

namespace capapp::capture {
    std::unique_ptr<ICaptureDevice> create_capture_device();
}

namespace capapp::decoder {
    std::unique_ptr<IDecoder> create_hardware_decoder();
}

// Naming: {Device}::{Decoder}::When{Condition}
TEST(CaptureDevice_DXGI, WhenStreamStarted) {
    auto dev = create_capture_device();
    CaptureDeviceConfig config{};
    config.targetWidth = 1920;
    config.targetHeight = 1080;
    config.targetFps = 60;
    config.enableZeroCopy = true;

    ASSERT_TRUE(dev->open(config));
    EXPECT_TRUE(dev->is_open());
    EXPECT_FALSE(dev->is_streaming());

    ASSERT_TRUE(dev->start_stream());
    EXPECT_TRUE(dev->is_streaming());

    Frame frame{};
    EXPECT_TRUE(dev->acquire_frame(frame));
    EXPECT_EQ(frame.width, 1920u);
    EXPECT_EQ(frame.height, 1080u);
    EXPECT_NE(frame.vkResourceHandle, nullptr); // Zero-copy GPU surface attached
    EXPECT_GT(frame.timestampNs, 0u);

    dev->release_frame(frame);
    CaptureDeviceStats stats = dev->stats();
    EXPECT_EQ(stats.framesCaptured, 1u);

    dev->stop_stream();
    EXPECT_FALSE(dev->is_streaming());
}

TEST(CaptureDevice_Decoder, WhenPacketDecodedHardwareAccelerated) {
    auto decoder = create_hardware_decoder();
    DecoderConfig config{};
    config.codec = DecoderCodec::H264;
    config.width = 1920;
    config.height = 1080;
    config.enableHardwareAcceleration = true;
    config.enableZeroCopyVulkanImport = true;

    ASSERT_TRUE(decoder->init(config));
    EXPECT_TRUE(decoder->is_initialized());
    EXPECT_TRUE(decoder->is_hardware_accelerated());

    uint8_t dummyNalu[32] = {0x00, 0x00, 0x00, 0x01, 0x67};
    TimestampNs pts = 33'333'333; // ~33.3ms
    EXPECT_TRUE(decoder->submit_packet(dummyNalu, sizeof(dummyNalu), pts, true));

    Frame decodedFrame{};
    EXPECT_TRUE(decoder->receive_decoded_frame(decodedFrame));
    EXPECT_EQ(decodedFrame.timestampNs, pts);
    EXPECT_EQ(decodedFrame.width, 1920u);
    EXPECT_EQ(decodedFrame.height, 1080u);
    EXPECT_NE(decodedFrame.vkResourceHandle, nullptr);
    EXPECT_TRUE(decodedFrame.isKeyframe);

    DecoderStats stats = decoder->stats();
    EXPECT_EQ(stats.framesDecoded, 1u);
    EXPECT_GT(stats.averageDecodeTimeNs, 0u);
}

TEST(CaptureDevice_Sync, WhenAudioVideoTimestampsAligned) {
    VulkanContext vkCtx;
    ASSERT_TRUE(vkCtx.initialize());

    AVSyncPipeline pipeline(&vkCtx);

    TimestampNs alignedTime = 1'000'000'000ULL; // 1.0s

    Frame video{};
    video.width = 1920;
    video.height = 1080;
    video.timestampNs = alignedTime;
    video.sequenceNumber = 100;

    AudioBuffer audio{};
    audio.sampleRate = 48000;
    audio.channels = 2;
    audio.frameCount = 480;
    audio.timestampNs = alignedTime + 2'000'000ULL; // 2ms skew (well within 20ms threshold)

    EXPECT_TRUE(pipeline.push_video_frame(video));
    EXPECT_TRUE(pipeline.push_audio_buffer(audio));

    Frame pairedVideo{};
    AudioBuffer pairedAudio{};
    EXPECT_TRUE(pipeline.get_synchronized_pair(pairedVideo, pairedAudio, 20'000'000));

    EXPECT_EQ(pairedVideo.sequenceNumber, 100u);
    EXPECT_EQ(pairedAudio.frameCount, 480u);

    SyncStats stats = pipeline.stats();
    EXPECT_EQ(stats.matchedAVPairs, 1u);
    EXPECT_LE(std::abs(stats.currentDriftNs), 2'000'000LL);
}

TEST(CaptureDevice_Sync, WhenDriftExceedsThreshold_CompensatesWithoutDeadlock) {
    AVSyncPipeline pipeline;

    // Stale video from long ago
    Frame staleVideo{};
    staleVideo.timestampNs = 500'000'000ULL; // 0.5s
    EXPECT_TRUE(pipeline.push_video_frame(staleVideo));

    // Fresh video
    Frame freshVideo{};
    freshVideo.timestampNs = 1'000'000'000ULL; // 1.0s
    EXPECT_TRUE(pipeline.push_video_frame(freshVideo));

    // Current audio
    AudioBuffer currentAudio{};
    currentAudio.timestampNs = 1'000'000'000ULL; // 1.0s
    EXPECT_TRUE(pipeline.push_audio_buffer(currentAudio));

    Frame pairedVideo{};
    AudioBuffer pairedAudio{};
    // Pipeline should drop staleVideo and match freshVideo with currentAudio
    EXPECT_TRUE(pipeline.get_synchronized_pair(pairedVideo, pairedAudio, 20'000'000));
    EXPECT_EQ(pairedVideo.timestampNs, freshVideo.timestampNs);

    SyncStats stats = pipeline.stats();
    EXPECT_EQ(stats.matchedAVPairs, 1u);
    EXPECT_EQ(stats.droppedVideoFrames, 1u);
}
