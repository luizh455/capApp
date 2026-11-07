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
    }
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
