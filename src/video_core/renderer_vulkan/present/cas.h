// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/math_util.h"
#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class VKDevice;
class VKScheduler;
class VKMemoryManager;

class CAS {
public:
    explicit CAS(const VKDevice& device, VKScheduler& scheduler, VKMemoryManager& memory_manager);
    ~CAS();

    void Configure(u32 input_width, u32 input_height, u32 output_width, u32 output_height,
                   float sharpness = 0.5f);
    void Draw(vk::ImageView input_image_view, vk::ImageView output_image_view);

private:
    void CreateShaderModules();
    void CreateDescriptorPool();
    void CreateDescriptorSetLayout();
    void CreateDescriptorSets();
    void CreatePipelineLayout();
    void CreatePipeline();

    const VKDevice& device;
    VKScheduler& scheduler;
    VKMemoryManager& memory_manager;

    vk::ShaderModule compute_shader;
    vk::DescriptorPool descriptor_pool;
    vk::DescriptorSetLayout descriptor_set_layout;
    vk::DescriptorSets descriptor_set;
    vk::PipelineLayout pipeline_layout;
    vk::Pipeline pipeline;

    struct CASConfiguration {
        float sharpness;
        u32 input_width;
        u32 input_height;
        u32 output_width;
        u32 output_height;
    } config{};

    vk::Buffer uniform_buffer;
    vk::DeviceMemory uniform_buffer_commit;
};

} // namespace Vulkan