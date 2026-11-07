#include <gtest/gtest.h>
#include <capapp/ring_buffer.hpp>
#include <capapp/frame.hpp>
#include <capapp/audio_buffer.hpp>

#include <thread>
#include <vector>
#include <numeric>
#include <chrono>

using namespace capapp;

// Test naming following AGENTS.md: {Device}::{Decoder}::When{Condition}
TEST(RingBuffer_Queue, WhenPushedAndPoppedSingleThreaded) {
    RingBuffer<int> rb(8);
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);

    EXPECT_TRUE(rb.push(42));
    EXPECT_TRUE(rb.push(100));
    EXPECT_FALSE(rb.empty());
    EXPECT_EQ(rb.size(), 2u);

    int val = 0;
    EXPECT_TRUE(rb.pop(val));
    EXPECT_EQ(val, 42);

    EXPECT_TRUE(rb.pop(val));
    EXPECT_EQ(val, 100);

    EXPECT_FALSE(rb.pop(val));
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer_Queue, WhenBufferReachesCapacity_RejectsSubsequentPush) {
    RingBuffer<uint32_t> rb(4);
    size_t cap = rb.capacity();

    for (uint32_t i = 0; i < cap; ++i) {
        EXPECT_TRUE(rb.push(i));
    }

    EXPECT_TRUE(rb.full());
    EXPECT_FALSE(rb.push(999)); // Must reject when full

    uint32_t outVal = 0;
    EXPECT_TRUE(rb.pop(outVal));
    EXPECT_EQ(outVal, 0u);
    EXPECT_FALSE(rb.full());

    EXPECT_TRUE(rb.push(999)); // Should succeed now
}

TEST(RingBuffer_Queue, WhenConcurrentProducerConsumer_ExpectNoTornOrDroppedItems) {
    constexpr size_t ITEM_COUNT = 100'000;
    RingBuffer<uint64_t> rb(1024);

    std::vector<uint64_t> consumed;
    consumed.reserve(ITEM_COUNT);

    std::atomic<bool> producerDone{false};

    std::thread producer([&]() {
        for (uint64_t i = 0; i < ITEM_COUNT; ++i) {
            while (!rb.push(i)) {
                std::this_thread::yield();
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        uint64_t val = 0;
        while (!producerDone.load(std::memory_order_acquire) || !rb.empty()) {
            if (rb.pop(val)) {
                consumed.push_back(val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(consumed.size(), ITEM_COUNT);
    for (size_t i = 0; i < ITEM_COUNT; ++i) {
        if (consumed[i] != i) {
            FAIL() << "Data mismatch at index " << i << ": expected " << i << ", got " << consumed[i];
        }
    }
}

TEST(CaptureDevice_Video, WhenFrameAllocatedWithNanoseconds) {
    Frame frame{};
    frame.width = 1920;
    frame.height = 1080;
    frame.stride = 1920 * 4;
    frame.format = PixelFormat::BGRA8;
    frame.timestampNs = 16'666'666; // ~16.6ms for 60fps in nanoseconds
    frame.sequenceNumber = 1;

    uint8_t dummyBuffer[64];
    frame.data = dummyBuffer;
    frame.dataSize = sizeof(dummyBuffer);

    EXPECT_TRUE(frame.is_valid());
    EXPECT_EQ(frame.compute_expected_size(), 1920u * 1080u * 4u);
    EXPECT_EQ(pixel_format_name(frame.format), "BGRA8");
}

TEST(CaptureDevice_Audio, WhenAudioBufferDurationCalculatedInNanoseconds) {
    AudioBuffer audio{};
    audio.sampleRate = 48000;
    audio.channels = 2;
    audio.bitsPerSample = 32;
    audio.format = AudioSampleFormat::Float32;
    audio.frameCount = 480; // 10ms at 48kHz
    audio.timestampNs = 1'000'000'000; // 1 second

    uint8_t dummyData[16];
    audio.data = dummyData;
    audio.dataSizeBytes = sizeof(dummyData);

    EXPECT_TRUE(audio.is_valid());
    EXPECT_EQ(audio.bytes_per_frame(), 8u); // 2 channels * 4 bytes
    // 480 frames at 48000Hz = 0.01s = 10,000,000 ns
    EXPECT_EQ(audio.duration_ns(), 10'000'000ULL);
    EXPECT_EQ(audio_format_name(audio.format), "Float32");
}
