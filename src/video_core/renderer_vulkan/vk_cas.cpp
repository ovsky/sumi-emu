// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/renderer_vulkan/vk_cas.h"
#include "video_core/renderer_vulkan/vk_device.h"
#include "video_core/renderer_vulkan/vk_memory_allocator.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h" // For LoadSPIRV
#include "video_core/renderer_vulkan/vk_util.h"     // For ImageMemoryBarrier helper (if you have one)  

// Path to your compiled CAS SPIR-V shader
// You will need to compile cas.comp to cas.comp.spv using glslangValidator
const char CAS_COMPUTE_SHADER_SPV_PATH[] = "shaders/cas.comp.spv";

namespace Vulkan {

// Helper to convert float to u32 for UBO packing if needed by shader
inline u32 FloatAsPackedU32(float val) {
return bit_cast<u32>(val);
}

CAS::CAS(const Device& device, MemoryAllocator& memory_allocator, size_t num_images, VkExtent2D max_target_size)
: device_ref(device), allocator_ref(memory_allocator), image_count(num_images), max_extent_(max_target_size) {

const VkSamplerCreateInfo sampler_info{
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, // CAS typically doesn't need mipmaps
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
};
linear_sampler_ = device_ref.GetLogical().CreateSampler(sampler_info);

CreateDescriptorSetLayout();
CreatePipelineLayout();
CreateComputePipeline(); // This depends on the shader module
CreateUniformBuffers();
CreateDescriptorPool();
CreateDescriptorSets(); // Allocates, actual binding in UpdateDescriptorSet
CreateOutputResources(max_target_size); // Initial creation with max size
}

CAS::~CAS() {
// Ensure GPU is idle before destroying resources if not handled by WrappedImage/View destructors
// device_ref.GetLogical().WaitIdle(); // Or a more specific scheduler wait
ReleaseOutputResources();
// Other vk::Unique handles clean themselves up.
}

void CAS::CreateOutputResources(VkExtent2D target_size) {
if (output_images_.size() == image_count &&
current_output_extent_.width == target_size.width &&
current_output_extent_.height == target_size.height) {
return; // Already correctly sized
}

ReleaseOutputResources(); // Release old resources if any
current_output_extent_ = target_size;

output_images_.resize(image_count);
output_image_views_.resize(image_count);

// Use a format that supports storage image usage and is suitable for presentation/further sampling.
// This should match the input format if CAS is just sharpening.
// VK_FORMAT_B8G8R8A8_UNORM is common for swapchains.
// VK_FORMAT_R8G8B8A8_UNORM or VK_FORMAT_A8B8G8R8_UNORM_PACK32 can also be used.
// Ensure it's compatible with the input from FSR/AA.
VkFormat image_format = VK_FORMAT_B8G8R8A8_UNORM; // Example, adjust if necessary

for (size_t i = 0; i < image_count; ++i) {
    const VkImageCreateInfo image_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = image_format,
        .extent = {current_output_extent_.width, current_output_extent_.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        // Usage: Storage for compute shader, Sampled for next stage (presentation)
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    output_images_[i] = allocator_ref.CreateImage(image_ci, MemoryUsage::GpuOnly);

    const VkImageViewCreateInfo view_ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *output_images_[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    output_image_views_[i] = device_ref.GetLogical().CreateImageView(view_ci);
}
// After creating new images, descriptor sets need to be updated if they were bound to old views
// This is handled in Draw() by calling UpdateDescriptorSet with the *new* output_image_view.
}

void CAS::ReleaseOutputResources() {
// Proper synchronization needed here if resources are in use by GPU
// For simplicity, assuming WrappedImage/View handles or external sync.
output_images_.clear();
output_image_views_.clear();
}

void CAS::CreateDescriptorSetLayout() {
const std::array bindings = {
// Binding 0: Input Texture (Combined Image Sampler)
vk::DescriptorSetLayoutBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
// Binding 1: Output Texture (Storage Image)
vk::DescriptorSetLayoutBinding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
// Binding 2: CAS Constants UBO
vk::DescriptorSetLayoutBinding{2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
};
const VkDescriptorSetLayoutCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
.bindingCount = static_cast<u32>(bindings.size()),
.pBindings = bindings.data(),
};
descriptor_set_layout_ = device_ref.GetLogical().CreateDescriptorSetLayout(ci);
}  

void CAS::CreatePipelineLayout() {
const VkPushConstantRange push_constant_range{
.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
.offset = 0,
.size = sizeof(float), // Sharpness amount
};
const VkPipelineLayoutCreateInfo ci{
.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
.setLayoutCount = 1,
.pSetLayouts = &(*descriptor_set_layout_),
.pushConstantRangeCount = 1,
.pPushConstantRanges = &push_constant_range,
};
pipeline_layout_ = device_ref.GetLogical().CreatePipelineLayout(ci);
}  

void CAS::CreateComputePipeline() {
auto cas_comp_module = LoadSPIRV(device_ref.GetLogical(), CAS_COMPUTE_SHADER_SPV_PATH);
if (!cas_comp_module) {
// Handle error: shader module failed to load
// You might throw an exception or log an error
LOG_CRITICAL(Render_Vulkan, "Failed to load CAS compute shader: {}", CAS_COMPUTE_SHADER_SPV_PATH);
return;
}

const VkPipelineShaderStageCreateInfo stage_ci{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = *cas_comp_module,
    .pName = "main", // Entry point of the shader
};
const VkComputePipelineCreateInfo ci{
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = stage_ci,
    .layout = *pipeline_layout_,
};
VkResult result;
std::tie(result, compute_pipeline_) = device_ref.GetLogical().CreateComputePipelines(VK_NULL_HANDLE, ci);
VK_ASSERT(result);
}

void CAS::CreateUniformBuffers() {
uniform_buffers_.resize(image_count);
const VkBufferCreateInfo ci = {
.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
.size = sizeof(CASConstants),
.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
};
for (size_t i = 0; i < image_count; ++i) {
uniform_buffers_[i] = allocator_ref.CreateBuffer(ci, MemoryUsage::CpuToGpu); // CPU Writable
}
}

void CAS::CreateDescriptorPool() {
const std::array pool_sizes = {
VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, image_count},
VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, image_count},
VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, image_count},
};
   const VkDescriptorPoolCreateInfo ci{
.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, // Allow freeing individual sets
.maxSets = image_count,
.poolSizeCount = static_cast<u32>(pool_sizes.size()),
.pPoolSizes = pool_sizes.data(),
};
descriptor_pool_ = device_ref.GetLogical().CreateDescriptorPool(ci);
}  

void CAS::CreateDescriptorSets() {
std::vector<VkDescriptorSetLayout> layouts(image_count, *descriptor_set_layout_);
const VkDescriptorSetAllocateInfo alloc_info{
.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
.descriptorPool = *descriptor_pool_,
.descriptorSetCount = static_cast<u32>(image_count),
.pSetLayouts = layouts.data(),
};
descriptor_sets_.resize(image_count);
VK_ASSERT(device_ref.GetLogical().AllocateDescriptorSets(&alloc_info, descriptor_sets_.data()));
}  

void CAS::UpdateDescriptorSet(size_t image_index, VkImageView input_view, VkImageView output_view) {
const VkDescriptorImageInfo input_image_info{
.sampler = *linear_sampler_,
.imageView = input_view,
.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
};
const VkDescriptorImageInfo output_image_info{
.imageView = output_view, // This is the output_image_views_[image_index]
.imageLayout = VK_IMAGE_LAYOUT_GENERAL, // For storage image access
};
const VkDescriptorBufferInfo ubo_info{
.buffer = *uniform_buffers_[image_index],
.offset = 0,
.range = sizeof(CASConstants),
};

const std::array writes = {
    vk::WriteDescriptorSet{descriptor_sets_[image_index], 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, input_image_info},
    vk::WriteDescriptorSet{descriptor_sets_[image_index], 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, output_image_info},
    vk::WriteDescriptorSet{descriptor_sets_[image_index], 2, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ubo_info},
};
device_ref.GetLogical().UpdateDescriptorSets(writes, {});
}

VkImageView CAS::Draw(Scheduler& scheduler, size_t image_index,
VkImage input_image_handle, VkImageView input_image_view,
VkExtent2D current_image_extent, float sharpness_amount) {

// Ensure output resources match the current processing extent
if (current_output_extent_.width != current_image_extent.width ||
    current_output_extent_.height != current_image_extent.height) {
    scheduler.WaitIdle(); // Wait for GPU to finish using old resources before recreating
    CreateOutputResources(current_image_extent);
    // Descriptor sets will need to be updated with new output_image_views
}

// Update UBO
CASConstants constants{};
constants.packed_const0[0] = current_image_extent.width;
constants.packed_const0[1] = current_image_extent.height;
// For the full AMD CAS shader, you'd pack 1.0f/width, 1.0f/height, and potentially other constants.
// Example:
// constants.packed_const0[2] = FloatAsPackedU32(1.0f / current_image_extent.width);
// constants.packed_const0[3] = FloatAsPackedU32(1.0f / current_image_extent.height);
// This depends on what your cas.comp shader expects in CASConstantsBlock.

void* ubo_ptr = uniform_buffers_[image_index].Mapped();
std::memcpy(ubo_ptr, &constants, sizeof(CASConstants));
// Consider mapped_buffer.FlushAllocation() if memory is not HOST_COHERENT

// Update descriptor set with the correct input and this frame's output view
UpdateDescriptorSet(image_index, input_image_view, *output_image_views_[image_index]);

scheduler.Record([this, image_index, input_image_handle, sharpness_amount, current_image_extent]
                 (vk::CommandBuffer cmdbuf) {

    // Barrier 1: Transition input image for shader read
    Util::ImageMemoryBarrier(cmdbuf, input_image_handle,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, // Potential previous writes
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL, // Assume it could be general from FSR/AA output
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // Barrier 2: Transition CAS output image for shader write (storage image)
    Util::ImageMemoryBarrier(cmdbuf, *output_images_[image_index],
        0, // No need for srcAccessMask if layout is UNDEFINED or if it was last read
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, // Or its previous layout (e.g., SHADER_READ_ONLY_OPTIMAL if reusing)
        VK_IMAGE_LAYOUT_GENERAL,   // Storage images are typically used with GENERAL layout
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, // If undefined
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_COMPUTE, *compute_pipeline_);
    cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline_layout_, 0,
                              {descriptor_sets_[image_index]}, {});

    cmdbuf.PushConstants(*pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &sharpness_amount);

    // Standard CAS group sizes are 8x8 or 16x16. AMD often suggests 8x8.
    const u32 group_size_x = 8;
    const u32 group_size_y = 8;
    const u32 dispatch_x = (current_image_extent.width + group_size_x - 1) / group_size_x;
    const u32 dispatch_y = (current_image_extent.height + group_size_y - 1) / group_size_y;
    cmdbuf.Dispatch(dispatch_x, dispatch_y, 1);

    // Barrier 3: Transition CAS output image for sampling in the next stage (e.g., presentation)
    Util::ImageMemoryBarrier(cmdbuf, *output_images_[image_index],
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT, // For sampling by final quad
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT); // Assuming next stage is fragment shader
});

return *output_image_views_[image_index];
}

} // namespace Vulkan