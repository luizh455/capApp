#include <gtest/gtest.h>
#include <capapp/ui/post_processing.hpp>
#include <vector>

using namespace capapp::ui;

TEST(CaptureDevice_Shader, WhenPostProcessingFilterApplied) {
    uint32_t width = 8;
    uint32_t height = 8;
    std::vector<uint8_t> pixels(width * height * 4, 100);

    // Test Passthrough
    EXPECT_TRUE(PostProcessor::apply_filter(FilterMode::Passthrough, width, height, pixels.data(), 0));
    EXPECT_EQ(pixels[0], 100);

    // Test Scanlines (modifies alternating rows)
    EXPECT_TRUE(PostProcessor::apply_filter(FilterMode::Scanlines, width, height, pixels.data(), 0));
    EXPECT_EQ(pixels[0], 60); // 100 * 0.6 = 60 for row 0

    // Test HighContrast
    EXPECT_TRUE(PostProcessor::apply_filter(FilterMode::HighContrast, width, height, pixels.data(), 0));

    // Test Sharpen
    EXPECT_TRUE(PostProcessor::apply_filter(FilterMode::Sharpen, width, height, pixels.data(), 0));

    EXPECT_STRNE(filter_mode_name(FilterMode::Scanlines).data(), "");
}
