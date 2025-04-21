// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/common_types.h"
#include "common/div_ceil.h"
#include "common/settings.h"

// #include "video_core/fsr.h"
// #include "video_core/host_shaders/vulkan_fidelityfx_fsr_easu_fp16_frag_spv.h"
// #include "video_core/host_shaders/vulkan_fidelityfx_fsr_easu_fp32_frag_spv.h"
// #include "video_core/host_shaders/vulkan_fidelityfx_fsr_rcas_fp16_frag_spv.h"
// #include "video_core/host_shaders/vulkan_fidelityfx_fsr_rcas_fp32_frag_spv.h"
// #include "video_core/host_shaders/vulkan_fidelityfx_fsr_vert_spv.h"
// #include "video_core/renderer_vulkan/present/fsr.h"
// #include "video_core/renderer_vulkan/present/util.h"
// #include "video_core/renderer_vulkan/vk_scheduler.h"
// #include "video_core/renderer_vulkan/vk_shader_util.h"
// #include "video_core/vulkan_common/vulkan_device.h"


// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/present/cas.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

namespace {
#include "shaders/full_screen/cas.comp.inc"
} // namespace

CAS::CAS(const VKDevice& device_, VKScheduler& scheduler_, VKMemoryManager& memory_manager_)
    : device{device_}, scheduler{scheduler_}, memory_manager{memory_manager_} {
    CreateShaderModules();
    CreateDescriptorPool();
    CreateDescriptorSetLayout();
    CreateDescriptorSets();
    CreatePipelineLayout();
    CreatePipeline();

    // Create uniform buffer
    const auto dev = device.GetLogical();
    const auto buffer_create_info = vk::BufferCreateInfo{
        .size = sizeof(CASConfiguration),
        .usage = vk::BufferUsageFlagBits::eUniformBuffer,
        .sharingMode = vk::SharingMode::eExclusive,
    };
    uniform_buffer = dev.createBuffer(buffer_create_info);
    const auto mem_requirements = dev.getBufferMemoryRequirements(uniform_buffer);
    uniform_buffer_commit = memory_manager.Commit(uniform_buffer, mem_requirements, true);
    dev.bindBufferMemory(uniform_buffer, uniform_buffer_commit, 0);
}

CAS::~CAS() {
    const auto dev = device.GetLogical();
    dev.destroyShaderModule(compute_shader);
    dev.destroyDescriptorPool(descriptor_pool);
    dev.destroyDescriptorSetLayout(descriptor_set_layout);
    dev.destroyPipelineLayout(pipeline_layout);
    dev.destroyPipeline(pipeline);
    dev.destroyBuffer(uniform_buffer);
    memory_manager.Release(uniform_buffer_commit);
}

void CAS::Configure(u32 input_width, u32 input_height, u32 output_width, u32 output_height,
                    float sharpness) {
    config = {
        .sharpness = sharpness,
        .input_width = input_width,
        .input_height = input_height,
        .output_width = output_width,
        .output_height = output_height,
    };

    // Update uniform buffer
    void* data = memory_manager.Map(uniform_buffer_commit, 0, sizeof(CASConfiguration));
    std::memcpy(data, &config, sizeof(CASConfiguration));
    memory_manager.Unmap(uniform_buffer_commit);
}

void CAS::Draw(VkImage input_image_view, vk::ImageView output_image_view) {
    const auto dev = device.GetLogical();
    const auto cmdbuf = scheduler.GetCommandBuffer();

    // Update descriptor sets
    const vk::DescriptorImageInfo input_image_info{
        .imageView = input_image_view,
        .imageLayout = vk::ImageLayout::eGeneral,
    };

    const vk::DescriptorImageInfo output_image_info{
        .imageView = output_image_view,
        .imageLayout = vk::ImageLayout::eGeneral,
    };

    const vk::DescriptorBufferInfo uniform_buffer_info{
        .buffer = uniform_buffer,
        .range = sizeof(CASConfiguration),
    };

    const std::array writes{
        vk::WriteDescriptorSet{
            .dstSet = descriptor_set,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &output_image_info,
        },
        vk::WriteDescriptorSet{
            .dstSet = descriptor_set,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &input_image_info,
        },
        vk::WriteDescriptorSet{
            .dstSet = descriptor_set,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .pBufferInfo = &uniform_buffer_info,
        },
    };
    dev.updateDescriptorSets(writes, {});

    // Dispatch compute
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
    cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, descriptor_set, {});

    const u32 num_groups_x = (config.output_width + 15) / 16;
    const u32 num_groups_y = (config.output_height + 15) / 16;
    cmdbuf.dispatch(num_groups_x, num_groups_y, 1);
}

void CAS::CreateShaderModules() {
    const auto dev = device.GetLogical();
    const auto shader_create_info = vk::ShaderModuleCreateInfo{
        .codeSize = sizeof(cas_comp),
        .pCode = cas_comp,
    };
    compute_shader = dev.createShaderModule(shader_create_info);
}

void CAS::CreateDescriptorPool() {
    const auto dev = device.GetLogical();
    const std::array pool_sizes{
        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1},
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1},
        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1},
    };
    const auto create_info = vk::DescriptorPoolCreateInfo{
        .maxSets = 1,
        .poolSizeCount = static_cast<u32>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };
    descriptor_pool = dev.createDescriptorPool(create_info);
}

void CAS::CreateDescriptorSetLayout() {
    const auto dev = device.GetLogical();
    const std::array bindings{
        vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = 2,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        },
    };
    const auto create_info = vk::DescriptorSetLayoutCreateInfo{
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    descriptor_set_layout = dev.createDescriptorSetLayout(create_info);
}

void CAS::CreateDescriptorSets() {
    const auto dev = device.GetLogical();
    const auto allocate_info = vk::DescriptorSetAllocateInfo{
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_set_layout,
    };
    descriptor_set = dev.allocateDescriptorSets(allocate_info).front();
}

void CAS::CreatePipelineLayout() {
    const auto dev = device.GetLogical();
    const auto create_info = vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout,
    };
    pipeline_layout = dev.createPipelineLayout(create_info);
}

void CAS::CreatePipeline() {
    const auto dev = device.GetLogical();
    const auto stage_create_info = vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = compute_shader,
        .pName = "main",
    };
    const auto create_info = vk::ComputePipelineCreateInfo{
        .stage = stage_create_info,
        .layout = pipeline_layout,
    };
    pipeline = dev.createComputePipeline(nullptr, create_info).value;
}

} // namespace Vulkan