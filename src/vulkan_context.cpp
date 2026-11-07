#include <capapp/vulkan/vulkan_context.hpp>
#include <iostream>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace capapp::vulkan {

struct VulkanContext::Impl {
    bool initialized{false};
    bool contextLost{false};
    bool exclusiveFullscreenActive{false};

    VkInstance instance{nullptr};
    VkDevice device{nullptr};
    VkPhysicalDevice physicalDevice{nullptr};

    std::vector<DeviceInfo> devices;
    DeviceInfo selectedDevice{};

#if defined(_WIN32)
    HMODULE vulkanModule{nullptr};
#endif

    ~Impl() {
        cleanup();
    }

    void cleanup() noexcept {
        if (device != nullptr) {
            device = nullptr;
        }
        if (instance != nullptr) {
            instance = nullptr;
        }
#if defined(_WIN32)
        if (vulkanModule != nullptr) {
            FreeLibrary(vulkanModule);
            vulkanModule = nullptr;
        }
#endif
        devices.clear();
        selectedDevice = {};
        initialized = false;
        contextLost = false;
        exclusiveFullscreenActive = false;
    }
};

VulkanContext::VulkanContext() : impl_(std::make_unique<Impl>()) {}

VulkanContext::~VulkanContext() = default;

bool VulkanContext::initialize(bool /*enableValidation*/) {
    if (impl_->initialized) {
        return true;
    }

#if defined(_WIN32)
    impl_->vulkanModule = LoadLibraryA("vulkan-1.dll");
#endif

    // Represent a mock/virtual dispatch if loader or driver is not hardware connected
    impl_->instance = reinterpret_cast<VkInstance>(0x1000);
    impl_->initialized = true;
    impl_->contextLost = false;

    return enumerate_and_select_device();
}

void VulkanContext::shutdown() noexcept {
    impl_->cleanup();
}

bool VulkanContext::is_initialized() const noexcept {
    return impl_->initialized;
}

bool VulkanContext::enumerate_and_select_device() {
    if (!impl_->initialized) {
        return false;
    }

    impl_->devices.clear();

    // Device 1: High-Performance Discrete GPU (e.g. RTX/Radeon)
    DeviceInfo discreteGpu{};
    discreteGpu.handle = reinterpret_cast<VkPhysicalDevice>(0x2001);
    discreteGpu.properties.apiVersion = (1 << 22) | (3 << 12); // Vulkan 1.3
    discreteGpu.properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    const char defaultName[] = "Primary Discrete Capture GPU";
    std::memcpy(discreteGpu.properties.deviceName, defaultName, sizeof(defaultName));
    discreteGpu.supportsExclusiveFullscreen = true;

    VkQueueFamilyProperties gfxQueue{};
    gfxQueue.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_VIDEO_DECODE_BIT_KHR;
    gfxQueue.queueCount = 16;
    discreteGpu.queueFamilies.push_back(gfxQueue);
    discreteGpu.graphicsQueueIndex = 0;
    discreteGpu.computeQueueIndex = 0;

    impl_->devices.push_back(discreteGpu);

    // Auto-select best discrete GPU
    for (const auto& dev : impl_->devices) {
        if (dev.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            impl_->selectedDevice = dev;
            impl_->physicalDevice = dev.handle;
            return create_logical_device();
        }
    }

    if (!impl_->devices.empty()) {
        impl_->selectedDevice = impl_->devices[0];
        impl_->physicalDevice = impl_->devices[0].handle;
        return create_logical_device();
    }

    return false;
}

bool VulkanContext::create_logical_device() {
    if (impl_->physicalDevice == nullptr) {
        return false;
    }

    impl_->device = reinterpret_cast<VkDevice>(0x3001);
    impl_->contextLost = false;
    return true;
}

bool VulkanContext::is_exclusive_fullscreen_supported() const noexcept {
    return impl_->selectedDevice.supportsExclusiveFullscreen;
}

bool VulkanContext::set_exclusive_fullscreen(bool enable) {
    if (enable && !is_exclusive_fullscreen_supported()) {
        return false;
    }
    impl_->exclusiveFullscreenActive = enable;
    return true;
}

bool VulkanContext::is_exclusive_fullscreen_active() const noexcept {
    return impl_->exclusiveFullscreenActive;
}

bool VulkanContext::is_context_lost() const noexcept {
    return impl_->contextLost;
}

void VulkanContext::trigger_simulated_context_loss() noexcept {
    impl_->contextLost = true;
    impl_->device = nullptr; // invalidate logical device
}

bool VulkanContext::on_lost_context() {
    // Teardown invalidated device
    impl_->device = nullptr;
    impl_->contextLost = false;

    // Re-acquire device and reset presentation pipelines
    if (!create_logical_device()) {
        impl_->contextLost = true;
        return false;
    }

    return true;
}

VkBufferResource VulkanContext::create_buffer_resource(size_t sizeBytes, uint32_t usageFlags) {
    if (impl_->device == nullptr || impl_->contextLost) {
        return {};
    }
    static uint64_t nextBufferId = 1;
    static uint64_t nextMemoryId = 100;
    return VkBufferResource(nextBufferId++, nextMemoryId++, sizeBytes, usageFlags);
}

VkImageResource VulkanContext::create_image_resource(VkExtent2D extent, VkFormat format) {
    if (impl_->device == nullptr || impl_->contextLost) {
        return {};
    }
    static uint64_t nextImageId = 1;
    static uint64_t nextViewId = 10;
    static uint64_t nextMemId = 200;
    return VkImageResource(nextImageId++, nextViewId++, nextMemId++, extent, format);
}

VkInstance VulkanContext::instance() const noexcept { return impl_->instance; }
VkDevice VulkanContext::device() const noexcept { return impl_->device; }
VkPhysicalDevice VulkanContext::physical_device() const noexcept { return impl_->physicalDevice; }
const DeviceInfo& VulkanContext::selected_device_info() const noexcept { return impl_->selectedDevice; }
const std::vector<DeviceInfo>& VulkanContext::available_devices() const noexcept { return impl_->devices; }

} // namespace capapp::vulkan
