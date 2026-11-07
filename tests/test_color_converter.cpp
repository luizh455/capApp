#include <gtest/gtest.h>
#include <capapp/decoder/color_converter.hpp>
#include <vector>

using namespace capapp::decoder;

TEST(CaptureDevice_Decoder, WhenNV12ConvertedToBGRA) {
    uint32_t width = 4;
    uint32_t height = 4;
    size_t yStride = width;
    size_t uvStride = width;

    // Y plane = 255 (white luminance)
    std::vector<uint8_t> yPlane(width * height, 255);
    // UV plane = 128 (neutral chroma)
    std::vector<uint8_t> uvPlane((width * height) / 2, 128);
    std::vector<uint8_t> outBgra(width * height * 4, 0);

    bool ok = ColorConverter::nv12_to_bgra(
        yPlane.data(), uvPlane.data(), width, height, yStride, uvStride, outBgra.data(), ColorSpace::BT709);

    EXPECT_TRUE(ok);

    // Pixel 0 should be approx white (255, 255, 255, 255)
    EXPECT_GE(outBgra[0], 250); // B
    EXPECT_GE(outBgra[1], 250); // G
    EXPECT_GE(outBgra[2], 250); // R
    EXPECT_EQ(outBgra[3], 255); // A
}

TEST(CaptureDevice_Decoder, WhenYUY2ConvertedToBGRA) {
    uint32_t width = 4;
    uint32_t height = 2;
    size_t stride = width * 2;

    // YUY2 pattern: Y0=200, U=128, Y1=200, V=128
    std::vector<uint8_t> yuy2Data(width * height * 2);
    for (size_t i = 0; i < yuy2Data.size(); i += 4) {
        yuy2Data[i + 0] = 200; // Y0
        yuy2Data[i + 1] = 128; // U
        yuy2Data[i + 2] = 200; // Y1
        yuy2Data[i + 3] = 128; // V
    }

    std::vector<uint8_t> outBgra(width * height * 4, 0);

    bool ok = ColorConverter::yuy2_to_bgra(
        yuy2Data.data(), width, height, stride, outBgra.data(), ColorSpace::BT601);

    EXPECT_TRUE(ok);
    EXPECT_NEAR(outBgra[0], 200, 5); // B
    EXPECT_NEAR(outBgra[1], 200, 5); // G
    EXPECT_NEAR(outBgra[2], 200, 5); // R
    EXPECT_EQ(outBgra[3], 255);       // A
}
