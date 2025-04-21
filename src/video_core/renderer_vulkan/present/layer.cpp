// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/present.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

#include "common/settings.h"
#include "video_core/framebuffer_config.h"
#include "video_core/renderer_vulkan/present/fsr.h"
#include "video_core/renderer_vulkan/present/fxaa.h"
#include "video_core/renderer_vulkan/present/layer.h"
#include "video_core/renderer_vulkan/present/present_push_constants.h"
#include "video_core/renderer_vulkan/present/smaa.h"
#include "video_core/renderer_vulkan/present/util.h"
#include <vulkan/vulkan.h> // Ensure Vulkan headers are included
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/textures/decoders.h"

// src/video_core/renderer_vulkan/layer.cpp
// Add these includes at the top:
#include "video_core/renderer_vulkan/cas_shader.h"
// #include "common/settings.h"

namespace Vulkan {

namespace {

u32 GetBytesPerPixel(const Tegra::FramebufferConfig& framebuffer) {
    using namespace VideoCore::Surface;
    return BytesPerBlock(PixelFormatFromGPUPixelFormat(framebuffer.pixel_format));
}

std::size_t GetSizeInBytes(const Tegra::FramebufferConfig& framebuffer) {
    return static_cast<std::size_t>(framebuffer.stride) *
           static_cast<std::size_t>(framebuffer.height) * GetBytesPerPixel(framebuffer);
}

VkFormat GetFormat(const Tegra::FramebufferConfig& framebuffer) {
    switch (framebuffer.pixel_format) {
    case Service::android::PixelFormat::Rgba8888:
    case Service::android::PixelFormat::Rgbx8888:
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    case Service::android::PixelFormat::Rgb565:
        return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case Service::android::PixelFormat::Bgra8888:
        return VK_FORMAT_B8G8R8A8_UNORM;
    default:
        UNIMPLEMENTED_MSG("Unknown framebuffer pixel format: {}",
                          static_cast<u32>(framebuffer.pixel_format));
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    }
}

} // Anonymous namespace

Layer::Layer(const Device& device_, MemoryAllocator& memory_allocator_, Scheduler& scheduler_,
             Tegra::MaxwellDeviceMemoryManager& device_memory_, size_t image_count_,
             VkExtent2D output_size, VkDescriptorSetLayout layout, const PresentFilters& filters_)
    : device(device_), memory_allocator(memory_allocator_), scheduler(scheduler_),
      device_memory(device_memory_), filters(filters_), image_count(image_count_) {
    CreateDescriptorPool();
    CreateDescriptorSets(layout);
    if (filters.get_scaling_filter() == Settings::ScalingFilter::Fsr) {
        CreateFSR(output_size);
    }
    if (filters.get_scaling_filter() == Settings::ScalingFilter::Cas) {
        CreateCASResources();
    }
}

Layer::~Layer() {
    ReleaseRawImages();
}

void Layer::ConfigureDraw(PresentPushConstants* out_push_constants,
                          VkDescriptorSet* out_descriptor_set, RasterizerVulkan& rasterizer,
                          VkSampler sampler, size_t image_index,
                          const Tegra::FramebufferConfig& framebuffer,
                          const Layout::FramebufferLayout& layout) {
    const auto texture_info = rasterizer.AccelerateDisplay(
        framebuffer, framebuffer.address + framebuffer.offset, framebuffer.stride);
    const u32 texture_width = texture_info ? texture_info->width : framebuffer.width;
    const u32 texture_height = texture_info ? texture_info->height : framebuffer.height;
    const u32 scaled_width = texture_info ? texture_info->scaled_width : texture_width;
    const u32 scaled_height = texture_info ? texture_info->scaled_height : texture_height;
    const bool use_accelerated = texture_info.has_value();

    RefreshResources(framebuffer);
    SetAntiAliasPass();

    // Finish any pending renderpass
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Wait(resource_ticks[image_index]);
    SCOPE_EXIT {
        resource_ticks[image_index] = scheduler.CurrentTick();
    };

    if (!use_accelerated) {
        UpdateRawImage(framebuffer, image_index);
    }

    VkImage source_image = texture_info ? texture_info->image : *raw_images[image_index];
    VkImageView source_image_view =
        texture_info ? texture_info->image_view : *raw_image_views[image_index];

    anti_alias->Draw(scheduler, image_index, &source_image, &source_image_view);

    auto crop_rect = Tegra::NormalizeCrop(framebuffer, texture_width, texture_height);
    const VkExtent2D render_extent{
        .width = scaled_width,
        .height = scaled_height,
    };

    if (fsr) {
        source_image_view = fsr->Draw(scheduler, image_index, source_image, source_image_view,
                                      render_extent, crop_rect);
        crop_rect = {0, 0, 1, 1};
    }

    SetMatrixData(*out_push_constants, layout);
    SetVertexData(*out_push_constants, layout, crop_rect);

    UpdateDescriptorSet(source_image_view, sampler, image_index);
    *out_descriptor_set = descriptor_sets[image_index];
}

void Layer::CreateDescriptorPool() {
    descriptor_pool = CreateWrappedDescriptorPool(device, image_count, image_count);
}

void Layer::CreateDescriptorSets(VkDescriptorSetLayout layout) {
    const std::vector layouts(image_count, layout);
    descriptor_sets = CreateWrappedDescriptorSets(descriptor_pool, layouts);
}

void Layer::CreateStagingBuffer(const Tegra::FramebufferConfig& framebuffer) {
    const VkBufferCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = CalculateBufferSize(framebuffer),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    buffer = memory_allocator.CreateBuffer(ci, MemoryUsage::Upload);
}

void Layer::CreateRawImages(const Tegra::FramebufferConfig& framebuffer) {
    const auto format = GetFormat(framebuffer);
    resource_ticks.resize(image_count);
    raw_images.resize(image_count);
    raw_image_views.resize(image_count);

    for (size_t i = 0; i < image_count; ++i) {
        raw_images[i] =
            CreateWrappedImage(memory_allocator, {framebuffer.width, framebuffer.height}, format);
        raw_image_views[i] = CreateWrappedImageView(device, raw_images[i], format);
    }
}

void Layer::CreateFSR(VkExtent2D output_size) {
    fsr = std::make_unique<FSR>(device, memory_allocator, image_count, output_size);
}

void Layer::RefreshResources(const Tegra::FramebufferConfig& framebuffer) {
    if (framebuffer.width == raw_width && framebuffer.height == raw_height &&
        framebuffer.pixel_format == pixel_format && !raw_images.empty()) {
        return;
    }

    raw_width = framebuffer.width;
    raw_height = framebuffer.height;
    pixel_format = framebuffer.pixel_format;
    anti_alias.reset();

    ReleaseRawImages();
    CreateStagingBuffer(framebuffer);
    CreateRawImages(framebuffer);
}

void Layer::SetAntiAliasPass() {
    if (anti_alias && anti_alias_setting == filters.get_anti_aliasing()) {
        return;
    }

    anti_alias_setting = filters.get_anti_aliasing();

    const VkExtent2D render_area{
        .width = Settings::values.resolution_info.ScaleUp(raw_width),
        .height = Settings::values.resolution_info.ScaleUp(raw_height),
    };

    switch (anti_alias_setting) {
    case Settings::AntiAliasing::Fxaa:
        anti_alias = std::make_unique<FXAA>(device, memory_allocator, image_count, render_area);
        break;
    case Settings::AntiAliasing::Smaa:
        anti_alias = std::make_unique<SMAA>(device, memory_allocator, image_count, render_area);
        break;
    default:
        anti_alias = std::make_unique<NoAA>();
        break;
    }
}

void Layer::ReleaseRawImages() {
    for (const u64 tick : resource_ticks) {
        scheduler.Wait(tick);
    }
    raw_images.clear();
    buffer.reset();
}

u64 Layer::CalculateBufferSize(const Tegra::FramebufferConfig& framebuffer) const {
    return GetSizeInBytes(framebuffer) * image_count;
}

u64 Layer::GetRawImageOffset(const Tegra::FramebufferConfig& framebuffer,
                             size_t image_index) const {
    return GetSizeInBytes(framebuffer) * image_index;
}

void Layer::SetMatrixData(PresentPushConstants& data,
                          const Layout::FramebufferLayout& layout) const {
    data.modelview_matrix =
        MakeOrthographicMatrix(static_cast<f32>(layout.width), static_cast<f32>(layout.height));
}

bool Layer::Initialize() {
    // Existing initialization code...

    // Initialize CAS resources
    if (!CreateCASResources()) {
        LOG_ERROR(Render_Vulkan, "Failed to initialize CAS resources");
        return false;
    }

    return true;
}

// Add CAS resource creation:
bool Layer::CreateCASResources() {
    // Create the shader module
    VkShaderModuleCreateInfo shader_module_ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = Vulkan::CAS_COMP_SHADER.size(),
        .pCode = reinterpret_cast<const uint32_t*>(Vulkan::CAS_COMP_SHADER.data()),
    };

        if (vkCreateShaderModule(device.GetLogical().GetVkDevice(), &shader_module_ci, nullptr, &cas_shader_module) != VK_SUCCESS) {
        // if (vkCreateShaderModule(device.GetLogical(), &shader_module_ci, nullptr, &cas_shader_module) != VK_SUCCESS) {
    // Remove the try-catch block as Vulkan C API does not throw exceptions.
    try {
        cas_shader_module = device.createShaderModule(shader_module_ci);
    } catch (const vk::SystemError& e) {
        LOG_ERROR(Render_Vulkan, "Failed to create CAS shader module: {}", e.what());
        return false;
    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
    VkDescriptorSetLayoutCreateInfo descriptor_layout_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = 2,
        .pBindings = bindings,
        if (vkCreateDescriptorSetLayout(device.GetLogical(), &descriptor_layout_ci, nullptr, &cas_descriptor_set_layout) != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to create CAS descriptor set layout");
            return false;
        }

    const vk::DescriptorSetLayoutCreateInfo descriptor_layout_ci{
        .bindingCount = static_cast<u32>(bindings.size()),
    VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 2,
    };
        cas_descriptor_set_layout = device.createDescriptorSetLayout(descriptor_layout_ci);
    VkDescriptorPoolCreateInfo descriptor_pool_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
        if (vkCreateDescriptorPool(device.GetLogical(), &descriptor_pool_ci, nullptr, &cas_descriptor_pool) != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to create CAS descriptor pool");
            return false;
        }
    // Create descriptor pool
    const vk::DescriptorPoolSize pool_size{
        .type = vk::DescriptorType::eStorageImage,
    VkDescriptorSetAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = cas_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &cas_descriptor_set_layout,
    };
        if (vkAllocateDescriptorSets(device.GetLogical(), &allocate_info, &cas_descriptor_set) != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to allocate CAS descriptor set");
            return false;
        }
        .pPoolSizes = &pool_size,
    };

    VkPushConstantRange push_constant{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(float) * 4, // Size of push constant block
    };
    }
    VkPipelineLayoutCreateInfo pipeline_layout_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &cas_descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant,
        if (vkCreatePipelineLayout(device.GetLogical(), &pipeline_layout_ci, nullptr, &cas_pipeline_layout) != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to create CAS pipeline layout");
            return false;
        }
    };

    try {
    VkPipelineShaderStageCreateInfo shader_stage{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = cas_shader_module,
    VkComputePipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = shader_stage,
        .layout = cas_pipeline_layout,
        if (vkCreateComputePipelines(device.GetLogical(), VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &cas_pipeline) != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "Failed to create CAS compute pipeline");
            return false;
        }
        .basePipelineIndex = -1,
    };
    const vk::PushConstantRange push_constant{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(float) * 4, // Size of push constant block
    };

    const vk::PipelineLayoutCreateInfo pipeline_layout_ci{
        .setLayoutCount = 1,
        .pSetLayouts = &cas_descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant,
    };

    try {
        cas_pipeline_layout = device.createPipelineLayout(pipeline_layout_ci);
    } catch (const vk::SystemError& e) {
        LOG_ERROR(Render_Vulkan, "Failed to create CAS pipeline layout: {}", e.what());
        return false;
    }

    // Create compute pipeline
    const vk::PipelineShaderStageCreateInfo shader_stage{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = cas_shader_module,
        .pName = "main",
    };

    const vk::ComputePipelineCreateInfo pipeline_ci{
        .stage = shader_stage,
        .layout = cas_pipeline_layout,
    };

    try {
        cas_pipeline = device.createComputePipeline(nullptr, pipeline_ci).value;
    } catch (const vk::SystemError& e) {
        LOG_ERROR(Render_Vulkan, "Failed to create CAS compute pipeline: {}", e.what());
        return false;
    }

    return true;
}

// Add method to update CAS descriptor set
void Layer::UpdateCASDescriptorSet(vk::ImageView input_view, vk::ImageView output_view) {
    const vk::DescriptorImageInfo input_image_info{
        .imageView = input_view,
        .imageLayout = vk::ImageLayout::eGeneral,
    };

    const vk::DescriptorImageInfo output_image_info{
        .imageView = output_view,
        .imageLayout = vk::ImageLayout::eGeneral,
    };

    const std::array<vk::WriteDescriptorSet, 2> writes{{
        {
            .dstSet = cas_descriptor_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &input_image_info,
        },
        {
            .dstSet = cas_descriptor_set,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &output_image_info,
        },
    }};

    device.updateDescriptorSets(writes, {});
}

// Add method to apply CAS
void Layer::ApplyCAS(vk::CommandBuffer cmdbuf) {
    // Similar to how FSR is implemented...

    // Transition input image to general layout for compute shader access
    const vk::ImageMemoryBarrier input_barrier{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame_buffer_image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                          vk::PipelineStageFlagBits::eComputeShader,
                          vk::DependencyFlagBits::eByRegion, {}, {}, input_barrier);

    // Transition output image to general layout for compute shader access
    const vk::ImageMemoryBarrier output_barrier{
        .srcAccessMask = {},
        .dstAccessMask = vk::AccessFlagBits::eShaderWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = presentation_image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                          vk::PipelineStageFlagBits::eComputeShader,
                          vk::DependencyFlagBits::eByRegion, {}, {}, output_barrier);

    // Update descriptor sets
    UpdateCASDescriptorSet(frame_buffer_image_view, presentation_image_view);

    // Bind pipeline and descriptor set
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, cas_pipeline);
    cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, cas_pipeline_layout, 0,
                             cas_descriptor_set, {});

    // Set push constants for CAS parameters
    const struct {
        float sharpness;
        float reserved1;
        float reserved2;
        float reserved3;
    } push_constants{
        .sharpness = Settings::values.cas_sharpness,
        .reserved1 = 0.0f,
        .reserved2 = 0.0f,
        .reserved3 = 0.0f,
    };

    cmdbuf.pushConstants(cas_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                        sizeof(push_constants), &push_constants);

    // Dispatch compute shader
    const u32 workgroups_x = (presentation_width + 7) / 8;
    const u32 workgroups_y = (presentation_height + 7) / 8;
    cmdbuf.dispatch(workgroups_x, workgroups_y, 1);

    // Transition output image back to transfer src layout
    const vk::ImageMemoryBarrier post_barrier{
        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = presentation_image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eTransfer,
                          vk::DependencyFlagBits::eByRegion, {}, {}, post_barrier);
}

// Modify the Layer::Render method to include CAS
void Layer::Render(vk::CommandBuffer cmdbuf) {
    // Existing rendering code...

    // Find where upscaling method is selected
    // Likely in the section where FSR is handled
    if (Settings::values.resolution_setup == Settings::ResolutionSetup::FSR) {
        // Existing FSR code...
    } else if (Settings::values.resolution_setup == Settings::ResolutionSetup::CAS) {
        // Apply CAS upscaling instead
        ApplyCAS(cmdbuf);
    } else {
        // Existing non-upscaling code...
    }

    // Rest of rendering code...
}

// Add CAS resource cleanup
void Layer::DestroyCASResources() {
    if (cas_pipeline) {
        device.destroyPipeline(cas_pipeline);
        cas_pipeline = {};
    }

    if (cas_pipeline_layout) {
        device.destroyPipelineLayout(cas_pipeline_layout);
        cas_pipeline_layout = {};
    }

    if (cas_descriptor_set_layout) {
        device.destroyDescriptorSetLayout(cas_descriptor_set_layout);
        cas_descriptor_set_layout = {};
    }

    if (cas_descriptor_pool) {
        device.destroyDescriptorPool(cas_descriptor_pool);
        cas_descriptor_pool = {};
    }

    if (cas_shader_module) {
        device.destroyShaderModule(cas_shader_module);
        cas_shader_module = {};
    }
}

// Update Layer destructor or cleanup method
void Layer::Destroy() {
    // Existing cleanup code...

    // Add CAS cleanup
    DestroyCASResources();

    // Rest of cleanup code...
}

void Layer::SetVertexData(PresentPushConstants& data, const Layout::FramebufferLayout& layout,
                          const Common::Rectangle<f32>& crop) const {
    // Map the coordinates to the screen.
    const auto& screen = layout.screen;
    const auto x = static_cast<f32>(screen.left);
    const auto y = static_cast<f32>(screen.top);
    const auto w = static_cast<f32>(screen.GetWidth());
    const auto h = static_cast<f32>(screen.GetHeight());

    data.vertices[0] = ScreenRectVertex(x, y, crop.left, crop.top);
    data.vertices[1] = ScreenRectVertex(x + w, y, crop.right, crop.top);
    data.vertices[2] = ScreenRectVertex(x, y + h, crop.left, crop.bottom);
    data.vertices[3] = ScreenRectVertex(x + w, y + h, crop.right, crop.bottom);
}

void Layer::UpdateDescriptorSet(VkImageView image_view, VkSampler sampler, size_t image_index) {
    const VkDescriptorImageInfo image_info{
        .sampler = sampler,
        .imageView = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    const VkWriteDescriptorSet sampler_write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = descriptor_sets[image_index],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };

    device.GetLogical().UpdateDescriptorSets(std::array{sampler_write}, {});
}

void Layer::UpdateRawImage(const Tegra::FramebufferConfig& framebuffer, size_t image_index) {
    const std::span<u8> mapped_span = buffer.Mapped();

    const u64 image_offset = GetRawImageOffset(framebuffer, image_index);

    const DAddr framebuffer_addr = framebuffer.address + framebuffer.offset;
    const u8* const host_ptr = device_memory.GetPointer<u8>(framebuffer_addr);

    // TODO(Rodrigo): Read this from HLE
    constexpr u32 block_height_log2 = 4;
    const u32 bytes_per_pixel = GetBytesPerPixel(framebuffer);
    const u64 linear_size{GetSizeInBytes(framebuffer)};
    const u64 tiled_size{Tegra::Texture::CalculateSize(
        true, bytes_per_pixel, framebuffer.stride, framebuffer.height, 1, block_height_log2, 0)};
    if (host_ptr) {
        Tegra::Texture::UnswizzleTexture(
            mapped_span.subspan(image_offset, linear_size), std::span(host_ptr, tiled_size),
            bytes_per_pixel, framebuffer.width, framebuffer.height, 1, block_height_log2, 0);
    }

    const VkBufferImageCopy copy{
        .bufferOffset = image_offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent =
            {
                .width = framebuffer.width,
                .height = framebuffer.height,
                .depth = 1,
            },
    };
    scheduler.Record([this, copy, index = image_index](vk::CommandBuffer cmdbuf) {
        const VkImage image = *raw_images[index];
        const VkImageMemoryBarrier base_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VkImageMemoryBarrier read_barrier = base_barrier;
        read_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        read_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        read_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        VkImageMemoryBarrier write_barrier = base_barrier;
        write_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        write_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        write_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                               read_barrier);
        cmdbuf.CopyBufferToImage(*buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy);
        cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               0, write_barrier);
    });
}

} // namespace Vulkan
