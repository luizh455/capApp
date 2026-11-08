#include <gtest/gtest.h>
#include <capapp/capture/camera_selector.hpp>

using namespace capapp;
using namespace capapp::capture;

// Naming: {Device}::{Decoder}::When{Condition}
TEST(CaptureDevice_Camera, WhenDevicesEnumerated) {
    auto devices = CameraSelector::enumerate_devices();
    EXPECT_FALSE(devices.empty());

    for (size_t i = 0; i < devices.size(); ++i) {
        EXPECT_EQ(devices[i].index, static_cast<uint32_t>(i));
        EXPECT_FALSE(devices[i].name.empty());
        EXPECT_GT(devices[i].width, 0u);
        EXPECT_GT(devices[i].height, 0u);
        EXPECT_GT(devices[i].maxFps, 0u);
        ASSERT_FALSE(devices[i].supportedCompressions.empty());
        EXPECT_EQ(devices[i].supportedCompressions.front(), CaptureCompression::Auto);
    }
}

TEST(CaptureDevice_Camera, WhenCompressionNameRequested_ReturnsReadableNames) {
    EXPECT_EQ(std::string(capture_compression_name(CaptureCompression::Auto)), "Auto");
    EXPECT_EQ(std::string(capture_compression_name(CaptureCompression::YUY2)), "YUY2");
    EXPECT_EQ(std::string(capture_compression_name(CaptureCompression::MJPEG)), "MJPEG");
}

TEST(CaptureDevice_Camera, WhenDeviceSelectedByIndex) {
    CameraSelector selector;
    EXPECT_GT(selector.device_count(), 0u);
    EXPECT_EQ(selector.active_index(), 0u);

    if (selector.device_count() > 1) {
        bool changedCalled = false;
        selector.onDeviceChanged = [&](const CameraDeviceInfo& dev) {
            changedCalled = true;
            EXPECT_EQ(dev.index, 1u);
        };

        EXPECT_TRUE(selector.select_device(1));
        EXPECT_TRUE(changedCalled);
        EXPECT_EQ(selector.active_index(), 1u);
        ASSERT_NE(selector.active_device(), nullptr);
        EXPECT_EQ(selector.active_device()->index, 1u);
    }

    // Invalid index must be rejected gracefully
    EXPECT_FALSE(selector.select_device(9999));
    EXPECT_EQ(selector.active_index(), selector.device_count() > 1 ? 1u : 0u);
}

TEST(CaptureDevice_Camera, WhenDeviceCycled) {
    CameraSelector selector;
    size_t count = selector.device_count();
    ASSERT_GT(count, 0u);

    uint32_t initialIndex = selector.active_index();
    const auto& nextDev = selector.cycle_next_device();
    uint32_t expectedNext = static_cast<uint32_t>((initialIndex + 1) % count);

    EXPECT_EQ(nextDev.index, expectedNext);
    EXPECT_EQ(selector.active_index(), expectedNext);

    const auto& prevDev = selector.cycle_prev_device();
    EXPECT_EQ(prevDev.index, initialIndex);
    EXPECT_EQ(selector.active_index(), initialIndex);
}

TEST(CaptureDevice_Camera, WhenSelectedByName) {
    CameraSelector selector;
    ASSERT_GT(selector.device_count(), 0u);

    const auto& firstDev = selector.devices()[0];
    EXPECT_TRUE(selector.select_device_by_name(firstDev.name));
    EXPECT_EQ(selector.active_index(), 0u);

    // Non-existent name
    EXPECT_FALSE(selector.select_device_by_name("NonExistentCameraDevice123456"));
}

TEST(CaptureDevice_Camera, WhenResolutionAndFpsCycled) {
    CameraSelector selector;
    bool formatChanged = false;
    uint32_t lastW = 0, lastH = 0, lastFps = 0;

    selector.onFormatChanged = [&](uint32_t w, uint32_t h, uint32_t fps) {
        formatChanged = true;
        lastW = w; lastH = h; lastFps = fps;
    };

    auto res1080 = selector.active_resolution();
    EXPECT_EQ(res1080.width, 1920u);
    EXPECT_EQ(res1080.height, 1080u);

    auto res1440 = selector.cycle_resolution();
    EXPECT_TRUE(formatChanged);
    EXPECT_EQ(res1440.width, 2560u);
    EXPECT_EQ(res1440.height, 1440u);
    EXPECT_EQ(lastW, 2560u);

    auto res4K = selector.cycle_resolution();
    EXPECT_EQ(res4K.width, 3840u);
    EXPECT_EQ(res4K.height, 2160u);

    uint32_t newFps = selector.cycle_fps();
    EXPECT_EQ(newFps, 30u);
    EXPECT_EQ(lastFps, 30u);

    EXPECT_TRUE(selector.set_resolution(1920, 1080));
    EXPECT_EQ(selector.active_resolution().width, 1920u);

    auto previousResolution = selector.cycle_resolution(-1);
    EXPECT_EQ(previousResolution.width, 3840u);
    EXPECT_EQ(previousResolution.height, 2160u);
}
