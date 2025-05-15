// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"
//#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class CAS {
public:
    explicit CAS(const Device& device, MemoryAllocator& allocator, size_t image_count);
    ~CAS();

    void Configure(vk::ImageView image_view, vk::Extent2D extent);
    void Draw(vk::CommandBuffer cmd, size_t image_index, vk::Extent2D output_extent);

private:
    void CreateImages();
    void CreateShaders();
    void CreateRenderPass();
    void CreateSampler();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void CreatePipelineLayout();
    void CreatePipeline();

    const Device& device;
    MemoryAllocator& allocator;
    const size_t image_count;

    vk::ShaderModule shader;
    vk::DescriptorSetLayout descriptor_set_layout;
    vk::DescriptorPool descriptor_pool;
    std::vector<vk::DescriptorSet> descriptor_sets;
    vk::PipelineLayout pipeline_layout;
    vk::Pipeline pipeline;
    vk::RenderPass render_pass;
    vk::Sampler sampler;

    struct Image {
        vk::Image image;
        vk::ImageView image_view;
        Allocation allocation;
    };
    std::vector<Image> images;

    struct RenderTarget {
        vk::Image image;
        vk::ImageView image_view;
        vk::Framebuffer framebuffer;
        Allocation allocation;
    };
    std::vector<RenderTarget> render_targets;

    vk::Extent2D extent{};
    vk::ImageView image_view{};
};

} // namespace Vulkan