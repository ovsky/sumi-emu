// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <vector>
#include "common/common_types.h"
//#include "video_core/renderer_vulkan/vk_common.h"
//#include "video_core/renderer_vulkan/vk_util.h" // For WrappedImageView, WrappedImage, etc.

namespace Vulkan {
class Device;
class MemoryAllocator;
class Scheduler;

// Constants for CAS UBO
// Note: AMD's CAS often uses 4x uvec4 for constants (const0 to const3) in its full shader.
// This is a simplified version. Adapt as needed based on the final shader.
// For just width, height, and precalculated 1/width, 1/height for the shader:
struct CASConstants {
    u32 packed_const0[4]; // input_width, input_height, 1.0f/input_width (as u32), 1.0f/input_height (as u32)
                          // Shader will unpack these. Or pass as separate uvec2 and vec2.
};

class CAS {
public:
    CAS(const Device& device, MemoryAllocator& memory_allocator, size_t num_images, VkExtent2D max_target_size);
    ~CAS();

    CAS(const CAS&) = delete;
    CAS& operator=(const CAS&) = delete;
    CAS(CAS&&) = default;
    CAS& operator=(CAS&&) = default;

    // Executes the CAS pass.
    // Returns a VkImageView to the sharpened output image for the given image_index.
    VkImageView Draw(Scheduler& scheduler, size_t image_index,
                     VkImage input_image, VkImageView input_image_view,
                     VkExtent2D current_image_extent, float sharpness_amount);

    // Optional: If CAS needs to know the output size it was created with (max_target_size)
    VkExtent2D GetMaxOutputExtent() const { return max_extent_;}

private:
    void CreateOutputResources(VkExtent2D target_size); // Create or recreate output images
    void ReleaseOutputResources();
    void CreateDescriptorSetLayout();
    void CreatePipelineLayout();
    void CreateComputePipeline();
    void CreateUniformBuffers();
    void CreateDescriptorPool();
    void CreateDescriptorSets(); // Allocates sets
    void UpdateDescriptorSet(size_t image_index, VkImageView input_view, VkImageView output_view); // Updates bindings

private:
    const Device& device_ref;
    MemoryAllocator& allocator_ref;
    const size_t image_count; // Number of in-flight frames / swapchain images

    std::vector<vk::Image> output_images_;
    std::vector<vk::ImageView> output_image_views_;
    std::vector<vk::Buffer> uniform_buffers_;
    std::vector<VkDescriptorSet> descriptor_sets_;

    vk::DescriptorSetLayout descriptor_set_layout_;
    vk::PipelineLayout pipeline_layout_;
    vk::Pipeline compute_pipeline_;
    vk::DescriptorPool descriptor_pool_;
    vk::Sampler linear_sampler_; // Sampler for reading the input texture

    VkExtent2D current_output_extent_{}; // Actual size of currently allocated output_images_
    VkExtent2D max_extent_{}; // Max size this instance was created for
};

} // namespace Vulkan