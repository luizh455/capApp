#pragma once

#include <capapp/vulkan/vulkan_types.hpp>
#include <capapp/vulkan/vulkan_resources.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace capapp::vulkan {

struct DeviceInfo {
    VkPhysicalDevice handle{nullptr};
    VkPhysicalDeviceProperties properties{};
    std::vector<VkQueueFamilyProperties> queueFamilies;
    uint32_t graphicsQueueIndex{UINT32_MAX};
    uint32_t computeQueueIndex{UINT32_MAX};
    bool supportsExclusiveFullscreen{false};
};

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    // Non-copyable
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    /**
     * @brief Initializes Vulkan runtime loader and creates VkInstance.
     */
    bool initialize(bool enableValidation = false);

    /**
     * @brief Shuts down Vulkan instance and cleans up device resources.
     */
    void shutdown() noexcept;

    [[nodiscard]] bool is_initialized() const noexcept;

    /**
     * @brief Enumerates physical GPUs and selects the highest performance discrete device.
     */
    bool enumerate_and_select_device();

    /**
     * @brief Creates VkDevice with graphics and low-latency transfer queues.
     */
    bool create_logical_device();

    /**
     * @brief Checks and toggles exclusive fullscreen presentation mode.
     */
    [[nodiscard]] bool is_exclusive_fullscreen_supported() const noexcept;
    bool set_exclusive_fullscreen(bool enable);
    [[nodiscard]] bool is_exclusive_fullscreen_active() const noexcept;

    /**
     * @brief Context Loss Handling (CaptureDevice::Vulkan::OnLostContext).
     * Recovers device, pipelines, and swapchain on VK_ERROR_DEVICE_LOST.
     */
    [[nodiscard]] bool is_context_lost() const noexcept;
    bool on_lost_context();
    void trigger_simulated_context_loss() noexcept;

    // Resource factory methods
    VkBufferResource create_buffer_resource(size_t sizeBytes, uint32_t usageFlags);
    VkImageResource create_image_resource(VkExtent2D extent, VkFormat format);

    // Getters
    [[nodiscard]] VkInstance instance() const noexcept;
    [[nodiscard]] VkDevice device() const noexcept;
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept;
    [[nodiscard]] const DeviceInfo& selected_device_info() const noexcept;
    [[nodiscard]] const std::vector<DeviceInfo>& available_devices() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace capapp::vulkan
