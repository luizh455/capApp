#include <capapp/decoder/color_converter.hpp>
#include <cmath>

namespace capapp::decoder {

bool ColorConverter::nv12_to_bgra(
    const uint8_t* yPlane,
    const uint8_t* uvPlane,
    uint32_t width,
    uint32_t height,
    size_t yStride,
    size_t uvStride,
    uint8_t* outBgra,
    ColorSpace cs) noexcept {
    if (!yPlane || !uvPlane || !outBgra || width == 0 || height == 0) {
        return false;
    }

    const auto coeffs = get_color_matrix(cs);

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* yRow = yPlane + (y * yStride);
        const uint8_t* uvRow = uvPlane + ((y / 2) * uvStride);
        uint8_t* bgraRow = outBgra + (y * width * 4);

        for (uint32_t x = 0; x < width; ++x) {
            float yVal = static_cast<float>(yRow[x]);
            uint32_t uvIndex = (x / 2) * 2;
            float uVal = static_cast<float>(uvRow[uvIndex]) - 128.0f;
            float vVal = static_cast<float>(uvRow[uvIndex + 1]) - 128.0f;

            float r = yVal + (coeffs.rV * vVal);
            float g = yVal + (coeffs.gU * uVal) + (coeffs.gV * vVal);
            float b = yVal + (coeffs.bU * uVal);

            uint32_t bgraIdx = x * 4;
            bgraRow[bgraIdx + 0] = clamp_u8(b);
            bgraRow[bgraIdx + 1] = clamp_u8(g);
            bgraRow[bgraIdx + 2] = clamp_u8(r);
            bgraRow[bgraIdx + 3] = 255;
        }
    }

    return true;
}

bool ColorConverter::yuy2_to_bgra(
    const uint8_t* yuy2Data,
    uint32_t width,
    uint32_t height,
    size_t stride,
    uint8_t* outBgra,
    ColorSpace cs) noexcept {
    if (!yuy2Data || !outBgra || width == 0 || height == 0) {
        return false;
    }

    const auto coeffs = get_color_matrix(cs);

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = yuy2Data + (y * stride);
        uint8_t* bgraRow = outBgra + (y * width * 4);

        for (uint32_t x = 0; x < width; x += 2) {
            uint32_t srcIdx = (x / 2) * 4;
            float y0 = static_cast<float>(srcRow[srcIdx + 0]);
            float u  = static_cast<float>(srcRow[srcIdx + 1]) - 128.0f;
            float y1 = static_cast<float>(srcRow[srcIdx + 2]);
            float v  = static_cast<float>(srcRow[srcIdx + 3]) - 128.0f;

            // Pixel 0 (y0, u, v)
            float r0 = y0 + (coeffs.rV * v);
            float g0 = y0 + (coeffs.gU * u) + (coeffs.gV * v);
            float b0 = y0 + (coeffs.bU * u);

            uint32_t outIdx0 = x * 4;
            bgraRow[outIdx0 + 0] = clamp_u8(b0);
            bgraRow[outIdx0 + 1] = clamp_u8(g0);
            bgraRow[outIdx0 + 2] = clamp_u8(r0);
            bgraRow[outIdx0 + 3] = 255;

            // Pixel 1 (y1, u, v)
            if (x + 1 < width) {
                float r1 = y1 + (coeffs.rV * v);
                float g1 = y1 + (coeffs.gU * u) + (coeffs.gV * v);
                float b1 = y1 + (coeffs.bU * u);

                uint32_t outIdx1 = (x + 1) * 4;
                bgraRow[outIdx1 + 0] = clamp_u8(b1);
                bgraRow[outIdx1 + 1] = clamp_u8(g1);
                bgraRow[outIdx1 + 2] = clamp_u8(r1);
                bgraRow[outIdx1 + 3] = 255;
            }
        }
    }

    return true;
}

} // namespace capapp::decoder
