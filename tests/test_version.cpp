#include <gtest/gtest.h>
#include <capapp/common.hpp>

// Naming convention per AGENTS.md: {Device}::{Decoder}::When{Condition}
TEST(CaptureCore, WhenVersionQueried) {
    const char* version = capapp::get_version_string();
    ASSERT_NE(version, nullptr);
    EXPECT_STRNE(version, "");
}

TEST(CaptureCore, WhenDefaultFrameInfoInitialized) {
    EXPECT_EQ(capapp::default_frame_info.width, 0u);
    EXPECT_EQ(capapp::default_frame_info.height, 0u);
    EXPECT_EQ(capapp::default_frame_info.timestampNs, 0u);
}
