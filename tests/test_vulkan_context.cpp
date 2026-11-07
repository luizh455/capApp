#include <gtest/gtest.h>
#include <capapp/vulkan/vulkan_context.hpp>
#include <capapp/vulkan/vulkan_resources.hpp>
#include <capapp/vulkan/shader_pipeline.hpp>

using namespace capapp::vulkan;

// Test naming exact match to AGENTS.md: CaptureDevice::Vulkan::OnLostContext
TEST(CaptureDevice_Vulkan, OnLostContext) {
    VulkanContext ctx;
    ASSERT_TRUE(ctx.initialize());
    ASSERT_TRUE(ctx.device() != nullptr);
    EXPECT_FALSE(ctx.is_context_lost());

    // Allocate resources before context loss
    VkBufferResource buf = ctx.create_buffer_resource(1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EXPECT_TRUE(buf.is_valid());

    // Simulate device lost event (VK_ERROR_DEVICE_LOST)
    ctx.trigger_simulated_context_loss();
    EXPECT_TRUE(ctx.is_context_lost());
    EXPECT_EQ(ctx.device(), nullptr);

    // Attempting to allocate during context lost must fail gracefully
    VkBufferResource failedBuf = ctx.create_buffer_resource(512, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    EXPECT_FALSE(failedBuf.is_valid());

    // Recover context
    bool recovered = ctx.on_lost_context();
    EXPECT_TRUE(recovered);
    EXPECT_FALSE(ctx.is_context_lost());
    EXPECT_TRUE(ctx.device() != nullptr);

    // After recovery, resource creation succeeds again
    VkBufferResource newBuf = ctx.create_buffer_resource(1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    EXPECT_TRUE(newBuf.is_valid());
}

TEST(CaptureDevice_Vulkan, WhenInitialized_SelectsDiscreteGpu) {
    VulkanContext ctx;
    ASSERT_TRUE(ctx.initialize());

    const auto& info = ctx.selected_device_info();
    EXPECT_EQ(info.properties.deviceType, VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
    EXPECT_TRUE(info.supportsExclusiveFullscreen);
    EXPECT_FALSE(info.queueFamilies.empty());
    EXPECT_NE(info.graphicsQueueIndex, UINT32_MAX);
}

TEST(CaptureDevice_Vulkan, WhenExclusiveFullscreenToggled) {
    VulkanContext ctx;
    ASSERT_TRUE(ctx.initialize());
    EXPECT_TRUE(ctx.is_exclusive_fullscreen_supported());
    EXPECT_FALSE(ctx.is_exclusive_fullscreen_active());

    EXPECT_TRUE(ctx.set_exclusive_fullscreen(true));
    EXPECT_TRUE(ctx.is_exclusive_fullscreen_active());

    EXPECT_TRUE(ctx.set_exclusive_fullscreen(false));
    EXPECT_FALSE(ctx.is_exclusive_fullscreen_active());
}

TEST(CaptureDevice_Vulkan, WhenResourcesAllocated) {
    VulkanContext ctx;
    ASSERT_TRUE(ctx.initialize());

    VkBufferResource buffer = ctx.create_buffer_resource(4096, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    EXPECT_TRUE(buffer.is_valid());
    EXPECT_EQ(buffer.size_bytes(), 4096u);
    EXPECT_NE(buffer.buffer(), VK_NULL_HANDLE);

    // Move semantics test
    VkBufferResource movedBuffer = std::move(buffer);
    EXPECT_FALSE(buffer.is_valid());
    EXPECT_TRUE(movedBuffer.is_valid());
    EXPECT_EQ(movedBuffer.size_bytes(), 4096u);

    // Image resource test
    VkExtent2D extent{1920, 1080};
    VkImageResource img = ctx.create_image_resource(extent, VK_FORMAT_B8G8R8A8_UNORM);
    EXPECT_TRUE(img.is_valid());
    EXPECT_EQ(img.extent().width, 1920u);
    EXPECT_EQ(img.extent().height, 1080u);
    EXPECT_EQ(img.format(), VK_FORMAT_B8G8R8A8_UNORM);
}

TEST(CaptureDevice_Shader, WhenStagesConfigured) {
    ShaderPipeline pipeline;
    EXPECT_FALSE(pipeline.is_complete_graphics_pipeline());

    // Dummy SPIR-V headers (0x07230203 is standard SPIR-V magic number)
    std::vector<uint32_t> vertSpirv = {0x07230203, 0x00010300, 0x00000000, 10, 0};
    std::vector<uint32_t> fragSpirv = {0x07230203, 0x00010300, 0x00000000, 12, 0};

    pipeline.add_stage(ShaderStage::Vertex, vertSpirv, "main");
    EXPECT_FALSE(pipeline.is_complete_graphics_pipeline());

    pipeline.add_stage(ShaderStage::Fragment, fragSpirv, "main");
    EXPECT_TRUE(pipeline.is_complete_graphics_pipeline());
    EXPECT_EQ(pipeline.stages().size(), 2u);
}
