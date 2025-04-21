#pragma once

//#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"

// #include "video_core/video_common/extent2d.h"

namespace Vulkan {

class CAS {
public:
    explicit CAS(const Device& device, MemoryAllocator& allocator, Scheduler& scheduler,
                DescriptorPool& descriptor_pool);
    ~CAS();
    // void Draw(const TextureCache& texture_cache, vk::ImageView image_view, VideoCommon::Extent2D extent);
    void Draw(const TextureCache& texture_cache, vk::ImageView image_view, VideoCommon::Extent2D extent);
    void SetSharpness(float sharpness);
    void SetExtent(VideoCommon::Extent2D new_extent);

private:
    void CreateRenderPass();
    void CreatePipeline();
    void CreateDescriptorSetLayout();
    void CreateDescriptorSets();
    void CreateFramebuffer();

    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    DescriptorPool& descriptor_pool;

    float sharpness = 0.5f;
    VideoCommon::Extent2D extent{};
    vk::RenderPass renderpass;
    vk::PipelineLayout layout;
    vk::Pipeline pipeline;
    vk::DescriptorSetLayout descriptor_set_layout;
    vk::DescriptorSets descriptor_set;
    vk::Image image;
    vk::ImageView image_view;
    vk::Framebuffer framebuffer;
    MemoryCommit allocation;
};

} // namespace Vulkan