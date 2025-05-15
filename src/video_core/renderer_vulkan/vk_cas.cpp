// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_cas.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"

namespace Vulkan {

namespace {
constexpr u32 CAS_SET = 0;
constexpr u32 CAS_BINDING_INPUT = 0;
constexpr u32 CAS_BINDING_OUTPUT = 1;

constexpr std::array<vk::DescriptorSetLayoutBinding, 2> CAS_BINDINGS = {{
    {CAS_BINDING_INPUT, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
    {CAS_BINDING_OUTPUT, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
}};

constexpr std::array CAS_SPV = std::to_array<uint32_t>({
#include "video_core/renderer_vulkan/shaders/amd_fidelityfx/cas.comp.spv"
});
} // Anonymous namespace

CAS::CAS(const Device& device_, MemoryAllocator& allocator_, size_t image_count_)
    : device{device_}, allocator{allocator_}, image_count{image_count_} {
    CreateShaders();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreatePipelineLayout();
    CreatePipeline();
    CreateImages();
}

CAS::~CAS() {
    const auto dev = device.GetLogical();
    dev.destroyPipeline(pipeline);
    dev.destroyPipelineLayout(pipeline_layout);
    dev.destroyDescriptorSetLayout(descriptor_set_layout);
    dev.destroyDescriptorPool(descriptor_pool);
    dev.destroyShaderModule(shader);
    dev.destroySampler(sampler);

    for (auto& target : render_targets) {
        dev.destroyFramebuffer(target.framebuffer);
        dev.destroyImageView(target.image_view);
        allocator.Release(target.image, target.allocation);
    }
    for (auto& image : images) {
        dev.destroyImageView(image.image_view);
        allocator.Release(image.image, image.allocation);
    }
}

void CAS::Configure(vk::ImageView image_view_, vk::Extent2D extent_) {
    image_view = image_view_;
    extent = extent_;

    for (size_t i = 0; i < image_count; ++i) {
        const vk::DescriptorImageInfo input_info(sampler, image_view, vk::ImageLayout::eGeneral);
        const vk::DescriptorImageInfo output_info(nullptr, images[i].image_view, vk::ImageLayout::eGeneral);

        const std::array<vk::WriteDescriptorSet, 2> writes = {{
            {descriptor_sets[i], CAS_BINDING_INPUT, 0, 1, vk::DescriptorType::eCombinedImageSampler, &input_info},
            {descriptor_sets[i], CAS_BINDING_OUTPUT, 0, 1, vk::DescriptorType::eStorageImage, &output_info},
        }};
        device.GetLogical().updateDescriptorSets(writes, {});
    }
}

// VkImageView FSR::Draw(Scheduler& scheduler, size_t image_index, VkImage source_image,
//     VkImageView source_image_view, VkExtent2D input_image_extent,
//     const Common::Rectangle<f32>& crop_rect) {

void CAS::Draw(vk::CommandBuffer cmd, size_t image_index, vk::Extent2D output_extent) {
    const auto& target = render_targets[image_index];

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, descriptor_sets[image_index], {});

    const u32 dispatch_x = (output_extent.width + 15) / 16;
    const u32 dispatch_y = (output_extent.height + 15) / 16;
    cmd.dispatch(dispatch_x, dispatch_y, 1);
}

void CAS::Draw(Scheduler& scheduler, size_t image_index, vk::Extent2D output_extent, VkImageView* source_image_view) {
    const auto& target = render_targets[image_index];

    UploadImages(scheduler);
    UpdateDescriptorSets(source_image_view, image_index);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([=](vk::CommandBuffer cmd) {

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout, 0, descriptor_sets[image_index], {});

        const u32 dispatch_x = (output_extent.width + 15) / 16;
        const u32 dispatch_y = (output_extent.height + 15) / 16;
        cmd.dispatch(dispatch_x, dispatch_y, 1);
    });
}

void CAS::CreateShaders() {
    shader = BuildShader(device.GetLogical(), CAS_SPV);
}

void CAS::CreateDescriptorPool() {
    const std::array<vk::DescriptorPoolSize, 2> pool_sizes = {{
        {vk::DescriptorType::eCombinedImageSampler, static_cast<u32>(image_count)},
        {vk::DescriptorType::eStorageImage, static_cast<u32>(image_count)},
    }};

    const vk::DescriptorPoolCreateInfo create_info(
        {}, static_cast<u32>(image_count), static_cast<u32>(pool_sizes.size()), pool_sizes.data());
    descriptor_pool = device.GetLogical().createDescriptorPool(create_info);
}

void CAS::CreateDescriptorSets() {
    const vk::DescriptorSetLayoutCreateInfo layout_info({}, static_cast<u32>(CAS_BINDINGS.size()), CAS_BINDINGS.data());
    descriptor_set_layout = device.GetLogical().createDescriptorSetLayout(layout_info);

    const std::vector layouts(image_count, descriptor_set_layout);
    const vk::DescriptorSetAllocateInfo allocate_info(descriptor_pool, static_cast<u32>(image_count), layouts.data());
    descriptor_sets = device.GetLogical().allocateDescriptorSets(allocate_info);
}

void CAS::CreatePipelineLayout() {
    const vk::PipelineLayoutCreateInfo layout_info({}, 1, &descriptor_set_layout);
    pipeline_layout = device.GetLogical().createPipelineLayout(layout_info);
}

void CAS::CreatePipeline() {
    const vk::ComputePipelineCreateInfo create_info(
        {}, {{}, vk::ShaderStageFlagBits::eCompute, shader, "main"}, pipeline_layout);
    pipeline = device.GetLogical().createComputePipeline(nullptr, create_info).value;
}

void CAS::CreateImages() {
    const vk::Device dev = device.GetLogical();
    const vk::Format format = vk::Format::eR8G8B8A8Unorm;

    images.resize(image_count);
    render_targets.resize(image_count);

    for (size_t i = 0; i < image_count; ++i) {
        // Create input image
        {
            const vk::ImageCreateInfo image_info({}, vk::ImageType::e2D, format,
                vk::Extent3D(extent.width, extent.height, 1), 1, 1, vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
                vk::SharingMode::eExclusive, 0, nullptr, vk::ImageLayout::eUndefined);

            auto& image = images[i];
            std::tie(image.image, image.allocation) = allocator.Create(image_info, MemoryUsage::DeviceLocal);
            image.image_view = dev.createImageView(vk::ImageViewCreateInfo(
                {}, image.image, vk::ImageViewType::e2D, format, {},
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
        }

        // Create render target
        {
            const vk::ImageCreateInfo image_info({}, vk::ImageType::e2D, format,
                vk::Extent3D(extent.width, extent.height, 1), 1, 1, vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage,
                vk::SharingMode::eExclusive, 0, nullptr, vk::ImageLayout::eUndefined);

            auto& target = render_targets[i];
            std::tie(target.image, target.allocation) = allocator.Create(image_info, MemoryUsage::DeviceLocal);
            target.image_view = dev.createImageView(vk::ImageViewCreateInfo(
                {}, target.image, vk::ImageViewType::e2D, format, {},
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
            target.framebuffer = dev.createFramebuffer(vk::FramebufferCreateInfo(
                {}, render_pass, 1, &target.image_view, extent.width, extent.height, 1));
        }
    }
}

} // namespace Vulkan