#pragma once

#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"

namespace Vulkan {

class CAS {
public:
    explicit CAS(const Device& device, MemoryAllocator& allocator, Scheduler& scheduler,
                DescriptorPool& descriptor_pool);
    ~CAS();

    void Draw(const TextureCache& texture_cache, vk::ImageView image_view, vk::Extent2D extent);
    void SetSharpness(float sharpness);

private:
    void CreateRenderPass();
    void CreatePipeline();
    void CreateDescriptorSetLayout();
    void CreateDescriptorSets();
    void CreateImages(vk::Extent2D extent);

    const Device& device;
    MemoryAllocator& allocator;
    Scheduler& scheduler;
    DescriptorPool& descriptor_pool;

    float sharpness = 0.5f;
    vk::Extent2D extent{};
    vk::UniqueRenderPass renderpass;
    vk::UniquePipelineLayout layout;
    vk::UniquePipeline pipeline;
    vk::UniqueDescriptorSetLayout descriptor_set_layout;
    vk::DescriptorSet descriptor_set;
    vk::UniqueImage image;
    vk::UniqueImageView image_view;
    MemoryCommit allocation;
};

} // namespace Vulkan