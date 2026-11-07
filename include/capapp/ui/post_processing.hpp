#pragma once

#include <capapp/frame.hpp>
#include <cstdint>
#include <string_view>
#include <algorithm>

namespace capapp::ui {

enum class FilterMode : uint8_t {
    Passthrough = 0,
    Scanlines,
    Sharpen,
    HighContrast
};

[[nodiscard]] constexpr std::string_view filter_mode_name(FilterMode mode) noexcept {
    switch (mode) {
        case FilterMode::Scanlines:    return "CRT Scanlines";
        case FilterMode::Sharpen:      return "Sharpening";
        case FilterMode::HighContrast: return "High Contrast";
        case FilterMode::Passthrough:
        default: return "Passthrough (Direct)";
    }
}

class PostProcessor {
public:
    static bool apply_filter(FilterMode mode, uint32_t width, uint32_t height, uint8_t* bgraData, uint64_t frameIndex) noexcept {
        if (!bgraData || width == 0 || height == 0) return false;
        if (mode == FilterMode::Passthrough) return true;

        if (mode == FilterMode::Scanlines) {
            for (uint32_t y = 0; y < height; ++y) {
                if ((y + frameIndex / 2) % 4 == 0) {
                    uint8_t* row = bgraData + (y * width * 4);
                    for (uint32_t x = 0; x < width; ++x) {
                        row[x * 4 + 0] = static_cast<uint8_t>(row[x * 4 + 0] * 0.6f);
                        row[x * 4 + 1] = static_cast<uint8_t>(row[x * 4 + 1] * 0.6f);
                        row[x * 4 + 2] = static_cast<uint8_t>(row[x * 4 + 2] * 0.6f);
                    }
                }
            }
        } else if (mode == FilterMode::HighContrast) {
            for (uint32_t y = 0; y < height; ++y) {
                uint8_t* row = bgraData + (y * width * 4);
                for (uint32_t x = 0; x < width; ++x) {
                    float b = row[x * 4 + 0] / 255.0f;
                    float g = row[x * 4 + 1] / 255.0f;
                    float r = row[x * 4 + 2] / 255.0f;

                    b = (b - 0.5f) * 1.5f + 0.5f;
                    g = (g - 0.5f) * 1.5f + 0.5f;
                    r = (r - 0.5f) * 1.5f + 0.5f;

                    row[x * 4 + 0] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
                    row[x * 4 + 1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
                    row[x * 4 + 2] = static_cast<uint8_t>(std::clamp(r * 255.0f, 0.0f, 255.0f));
                }
            }
        } else if (mode == FilterMode::Sharpen) {
            // Simple center-weight boost filter
            for (uint32_t y = 0; y < height; ++y) {
                uint8_t* row = bgraData + (y * width * 4);
                for (uint32_t x = 0; x < width; ++x) {
                    row[x * 4 + 0] = static_cast<uint8_t>(std::min(255, static_cast<int>(row[x * 4 + 0] * 1.15f)));
                    row[x * 4 + 1] = static_cast<uint8_t>(std::min(255, static_cast<int>(row[x * 4 + 1] * 1.15f)));
                    row[x * 4 + 2] = static_cast<uint8_t>(std::min(255, static_cast<int>(row[x * 4 + 2] * 1.15f)));
                }
            }
        }
        return true;
    }
};

} // namespace capapp::ui
