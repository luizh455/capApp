#pragma once

#include <capapp/frame.hpp>
#include <string>
#include <cstdint>

namespace capapp::decoder {

enum class DecoderCodec : uint32_t {
    Raw = 0,
    H264,
    HEVC,
    AV1
};

struct DecoderConfig {
    DecoderCodec codec{DecoderCodec::H264};
    uint32_t width{1920};
    uint32_t height{1080};
    PixelFormat outputFormat{PixelFormat::BGRA8};
    bool enableHardwareAcceleration{true};
    bool enableZeroCopyVulkanImport{true};
};

struct DecoderStats {
    uint64_t framesSubmitted{0};
    uint64_t framesDecoded{0};
    uint64_t decodeErrors{0};
    TimestampNs averageDecodeTimeNs{0};
};

class IDecoder {
public:
    virtual ~IDecoder() = default;

    virtual bool init(const DecoderConfig& config) = 0;
    virtual void reset() noexcept = 0;
    virtual bool is_initialized() const noexcept = 0;

    virtual bool submit_packet(const uint8_t* data, size_t sizeBytes, TimestampNs ptsNs, bool isKeyframe) = 0;
    virtual bool receive_decoded_frame(Frame& outFrame) = 0;

    [[nodiscard]] virtual bool is_hardware_accelerated() const noexcept = 0;
    [[nodiscard]] virtual DecoderStats stats() const noexcept = 0;
};

} // namespace capapp::decoder
