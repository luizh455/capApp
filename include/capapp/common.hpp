#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace capapp {

// High-resolution timestamp in nanoseconds
using TimestampNs = uint64_t;

// CapApp Versioning
inline constexpr uint32_t VERSION_MAJOR = 0;
inline constexpr uint32_t VERSION_MINOR = 1;
inline constexpr uint32_t VERSION_PATCH = 0;

[[nodiscard]] const char* get_version_string() noexcept;

// Frame struct marked inline to reduce allocation overhead per repository guidelines
inline struct FrameInfo {
    uint32_t width{0};
    uint32_t height{0};
    uint32_t format{0};
    TimestampNs timestampNs{0};
    uint64_t frameNumber{0};
} default_frame_info{};

} // namespace capapp
