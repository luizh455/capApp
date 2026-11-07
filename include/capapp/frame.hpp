#pragma once

#include <capapp/common.hpp>
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace capapp {

enum class PixelFormat : uint32_t {
    Unknown = 0,
    BGRA8,
    RGBA8,
    NV12,
    P010,
    YUY2,
    HDR10_ST2084
};

[[nodiscard]] constexpr std::string_view pixel_format_name(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::BGRA8: return "BGRA8";
        case PixelFormat::RGBA8: return "RGBA8";
        case PixelFormat::NV12:  return "NV12";
        case PixelFormat::P010:  return "P010";
        case PixelFormat::YUY2:  return "YUY2";
        case PixelFormat::HDR10_ST2084: return "HDR10_ST2084";
        default: return "Unknown";
    }
}

/**
 * @brief High-performance video frame descriptor.
 *
 * Defined inline to minimize allocation overhead per repository guidelines.
 * Carries pixel buffer references and nanosecond timestamp for A/V synchronization.
 */
inline struct Frame {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t stride{0};
    PixelFormat format{PixelFormat::BGRA8};
    TimestampNs timestampNs{0};
    uint64_t sequenceNumber{0};
    uint8_t* data{nullptr};
    size_t dataSize{0};
    bool isKeyframe{true};
    void* vkResourceHandle{nullptr}; // Reference to backing VkImageResource if GPU-allocated

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return (width > 0) && (height > 0) && (data != nullptr || vkResourceHandle != nullptr);
    }

    [[nodiscard]] constexpr size_t compute_expected_size() const noexcept {
        return static_cast<size_t>(stride > 0 ? stride : width * 4) * height;
    }
} default_frame{};

} // namespace capapp
