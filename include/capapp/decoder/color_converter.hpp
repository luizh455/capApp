#pragma once

#include <capapp/frame.hpp>
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace capapp::decoder {

enum class ColorSpace : uint8_t {
    BT601 = 0,
    BT709
};

struct ColorMatrixCoeffs {
    float rV{1.402f};
    float gU{-0.344136f};
    float gV{-0.714136f};
    float bU{1.772f};
};

[[nodiscard]] constexpr ColorMatrixCoeffs get_color_matrix(ColorSpace cs) noexcept {
    switch (cs) {
        case ColorSpace::BT709:
            return {1.5748f, -0.1873f, -0.4681f, 1.8556f};
        case ColorSpace::BT601:
        default:
            return {1.4020f, -0.344136f, -0.714136f, 1.7720f};
    }
}

class ColorConverter {
public:
    ColorConverter() = default;

    /**
     * @brief Converts NV12 (semi-planar Y + UV) to 32-bit BGRA format.
     */
    static bool nv12_to_bgra(
        const uint8_t* yPlane,
        const uint8_t* uvPlane,
        uint32_t width,
        uint32_t height,
        size_t yStride,
        size_t uvStride,
        uint8_t* outBgra,
        ColorSpace cs = ColorSpace::BT709) noexcept;

    /**
     * @brief Converts YUY2 (packed 4:2:2 YUYV) to 32-bit BGRA format.
     */
    static bool yuy2_to_bgra(
        const uint8_t* yuy2Data,
        uint32_t width,
        uint32_t height,
        size_t stride,
        uint8_t* outBgra,
        ColorSpace cs = ColorSpace::BT709) noexcept;

private:
    static inline uint8_t clamp_u8(float val) noexcept {
        return static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
    }
};

} // namespace capapp::decoder
