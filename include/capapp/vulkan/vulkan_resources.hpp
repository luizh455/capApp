#pragma once

#include <capapp/vulkan/vulkan_types.hpp>
#include <utility>
#include <cstdint>

namespace capapp::vulkan {

/**
 * @brief RAII buffer resource wrapper.
 * Named Vk*Resource per repository naming guidelines.
 */
class VkBufferResource {
public:
    VkBufferResource() noexcept = default;

    VkBufferResource(VkBuffer buffer, VkDeviceMemory memory, size_t sizeBytes, uint32_t usageFlags) noexcept
        : buffer_(buffer), memory_(memory), sizeBytes_(sizeBytes), usageFlags_(usageFlags) {}

    ~VkBufferResource() noexcept {
        release();
    }

    // Move-only semantics
    VkBufferResource(const VkBufferResource&) = delete;
    VkBufferResource& operator=(const VkBufferResource&) = delete;

    VkBufferResource(VkBufferResource&& other) noexcept
        : buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
          memory_(std::exchange(other.memory_, VK_NULL_HANDLE)),
          sizeBytes_(std::exchange(other.sizeBytes_, 0)),
          usageFlags_(std::exchange(other.usageFlags_, 0)) {}

    VkBufferResource& operator=(VkBufferResource&& other) noexcept {
        if (this != &other) {
            release();
            buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
            memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
            sizeBytes_ = std::exchange(other.sizeBytes_, 0);
            usageFlags_ = std::exchange(other.usageFlags_, 0);
        }
        return *this;
    }

    [[nodiscard]] VkBuffer buffer() const noexcept { return buffer_; }
    [[nodiscard]] VkDeviceMemory memory() const noexcept { return memory_; }
    [[nodiscard]] size_t size_bytes() const noexcept { return sizeBytes_; }
    [[nodiscard]] uint32_t usage_flags() const noexcept { return usageFlags_; }
    [[nodiscard]] bool is_valid() const noexcept { return buffer_ != VK_NULL_HANDLE; }

    void release() noexcept {
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        sizeBytes_ = 0;
        usageFlags_ = 0;
    }

private:
    VkBuffer buffer_{VK_NULL_HANDLE};
    VkDeviceMemory memory_{VK_NULL_HANDLE};
    size_t sizeBytes_{0};
    uint32_t usageFlags_{0};
};

/**
 * @brief RAII image and view resource wrapper.
 * Named Vk*Resource per repository naming guidelines.
 */
class VkImageResource {
public:
    VkImageResource() noexcept = default;

    VkImageResource(VkImage image, VkImageView imageView, VkDeviceMemory memory,
                    VkExtent2D extent, VkFormat format) noexcept
        : image_(image), imageView_(imageView), memory_(memory),
          extent_(extent), format_(format) {}

    ~VkImageResource() noexcept {
        release();
    }

    // Move-only semantics
    VkImageResource(const VkImageResource&) = delete;
    VkImageResource& operator=(const VkImageResource&) = delete;

    VkImageResource(VkImageResource&& other) noexcept
        : image_(std::exchange(other.image_, VK_NULL_HANDLE)),
          imageView_(std::exchange(other.imageView_, VK_NULL_HANDLE)),
          memory_(std::exchange(other.memory_, VK_NULL_HANDLE)),
          extent_(std::exchange(other.extent_, {0, 0})),
          format_(std::exchange(other.format_, VK_FORMAT_UNDEFINED)) {}

    VkImageResource& operator=(VkImageResource&& other) noexcept {
        if (this != &other) {
            release();
            image_ = std::exchange(other.image_, VK_NULL_HANDLE);
            imageView_ = std::exchange(other.imageView_, VK_NULL_HANDLE);
            memory_ = std::exchange(other.memory_, VK_NULL_HANDLE);
            extent_ = std::exchange(other.extent_, {0, 0});
            format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
        }
        return *this;
    }

    [[nodiscard]] VkImage image() const noexcept { return image_; }
    [[nodiscard]] VkImageView image_view() const noexcept { return imageView_; }
    [[nodiscard]] VkDeviceMemory memory() const noexcept { return memory_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] VkFormat format() const noexcept { return format_; }
    [[nodiscard]] bool is_valid() const noexcept { return image_ != VK_NULL_HANDLE; }

    void release() noexcept {
        image_ = VK_NULL_HANDLE;
        imageView_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        extent_ = {0, 0};
        format_ = VK_FORMAT_UNDEFINED;
    }

private:
    VkImage image_{VK_NULL_HANDLE};
    VkImageView imageView_{VK_NULL_HANDLE};
    VkDeviceMemory memory_{VK_NULL_HANDLE};
    VkExtent2D extent_{0, 0};
    VkFormat format_{VK_FORMAT_UNDEFINED};
};

} // namespace capapp::vulkan
