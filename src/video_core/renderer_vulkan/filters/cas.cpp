#include "cas.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

CAS::CAS(const Device& device_, MemoryAllocator& allocator_, Scheduler& scheduler_,
         DescriptorPool& descriptor_pool_)
    : device(device_), allocator(allocator_), scheduler(scheduler_),
      descriptor_pool(descriptor_pool_) {
    CreateDescriptorSetLayout();
    CreatePipeline();
}

void CAS::SetSharpness(float sharpness_) {
    sharpness = sharpness_;
}

void CAS::Draw(const TextureCache& texture_cache, vk::ImageView image_view, vk::Extent2D extent_) {
    if (extent.width != extent_.width || extent.height != extent_.height) {
        extent = extent_;
        CreateImages(extent);
    }

    const auto cmdbuf = scheduler.GetRenderCommandBuffer();
    const auto& dld = device.GetDispatchLoader();

    // Update descriptor set
    vk::DescriptorImageInfo image_info(texture_cache.GetSampler(), image_view,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);
    vk::WriteDescriptorSet descriptor_write(descriptor_set, 0, 0,
                                          vk::DescriptorType::eCombinedImageSampler,
                                          image_info);
    device.GetLogical().updateDescriptorSets(descriptor_write, {}, dld);

    // Begin render pass
    vk::RenderPassBeginInfo renderpass_bi;
    renderpass_bi.renderPass = *renderpass;
    renderpass_bi.framebuffer = *framebuffer;
    renderpass_bi.renderArea.extent = extent;

    cmdbuf.beginRenderPass(renderpass_bi, vk::SubpassContents::eInline, dld);

    // Bind pipeline and descriptor sets
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline, dld);
    cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layout, 0, descriptor_set, {}, dld);

    // Push constants
    struct PushConstants {
        float sharpness;
    } constants{sharpness};

    cmdbuf.pushConstants<PushConstants>(*layout, vk::ShaderStageFlagBits::eFragment, 0, constants, dld);

    // Draw
    cmdbuf.draw(3, 1, 0, 0, dld);

    cmdbuf.endRenderPass(dld);
}

void CAS::CreateDescriptorSetLayout() {
    const std::array bindings = {
        vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eCombinedImageSampler, 1,
                                     vk::ShaderStageFlagBits::eFragment),
    };

    descriptor_set_layout = device.GetLogical().createDescriptorSetLayoutUnique(
        vk::DescriptorSetLayoutCreateInfo({}, bindings), nullptr, device.GetDispatchLoader());
}

void CAS::CreatePipeline() {
    const auto code = CompileShader(ReadFile(VK_SHADERS_DIR "filters/cas.frag"),
                               vk::ShaderStageFlagBits::eFragment);
    const auto module = device.GetLogical().createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, code.size(), reinterpret_cast<const u32*>(code.data())),
        nullptr, device.GetDispatchLoader());

    const std::array stages = {
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex,
                                         *vert_shader, "main"),
        vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment,
                                         *module, "main"),
    };

    const std::array dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    const auto vertex_binding = vk::VertexInputBindingDescription(0, sizeof(float) * 2,
                              vk::VertexInputRate::eVertex);
    const auto vertex_attrib = vk::VertexInputAttributeDescription(0, 0,
                               vk::Format::eR32G32Sfloat, 0);

    vk::PipelineVertexInputStateCreateInfo vertex_input({}, vertex_binding, vertex_attrib);
    vk::PipelineInputAssemblyStateCreateInfo input_assembly({},
                                                          vk::PrimitiveTopology::eTriangleList);
    vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    vk::PipelineRasterizationStateCreateInfo rasterization({}, false, false,
                                                         vk::PolygonMode::eFill);
    rasterization.cullMode = vk::CullModeFlagBits::eNone;
    rasterization.frontFace = vk::FrontFace::eCounterClockwise;
    rasterization.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisample({}, vk::SampleCountFlagBits::e1);
    vk::PipelineDepthStencilStateCreateInfo depth_stencil;
    vk::PipelineColorBlendStateCreateInfo color_blend({}, false);
    color_blend.attachmentCount = 1;

    const vk::PipelineColorBlendAttachmentState blend_attachment(
        false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    color_blend.pAttachments = &blend_attachment;

    vk::PipelineDynamicStateCreateInfo dynamic_state({}, dynamic_states);

    vk::PushConstantRange push_constant(vk::ShaderStageFlagBits::eFragment, 0, sizeof(float));

    layout = device.GetLogical().createPipelineLayoutUnique(
        vk::PipelineLayoutCreateInfo({}, *descriptor_set_layout, push_constant),
        nullptr, device.GetDispatchLoader());

    pipeline = device.GetLogical().createGraphicsPipelineUnique(
        nullptr,
        vk::GraphicsPipelineCreateInfo(
            {}, stages, &vertex_input, &input_assembly, nullptr, &viewport, &rasterization,
            &multisample, &depth_stencil, &color_blend, &dynamic_state, *layout, *renderpass),
        nullptr, device.GetDispatchLoader());
}

void CAS::CreateDescriptorSets() {
    descriptor_set = descriptor_pool.Allocate(*descriptor_set_layout);
}

void CAS::CreateImages(vk::Extent2D new_extent) {
    extent = new_extent;

    // Create image
    vk::ImageCreateInfo image_ci;
    image_ci.imageType = vk::ImageType::e2D;
    image_ci.format = vk::Format::eB8G8R8A8Unorm;
    image_ci.extent = vk::Extent3D(extent.width, extent.height, 1);
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = vk::SampleCountFlagBits::e1;
    image_ci.tiling = vk::ImageTiling::eOptimal;
    image_ci.usage = vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eStorage;
    image_ci.sharingMode = vk::SharingMode::eExclusive;

    image = device.GetLogical().createImageUnique(image_ci, nullptr, device.GetDispatchLoader());

    // Allocate memory
    vk::MemoryRequirements mem_reqs = device.GetLogical().getImageMemoryRequirements(*image);
    allocation = allocator.Commit(mem_reqs, MemoryUsage::DeviceLocal);

    // Bind memory
    device.GetLogical().bindImageMemory(*image, allocation.memory, allocation.offset);

    // Create image view
    vk::ImageViewCreateInfo view_ci;
    view_ci.image = *image;
    view_ci.viewType = vk::ImageViewType::e2D;
    view_ci.format = vk::Format::eB8G8R8A8Unorm;
    view_ci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;

    image_view = device.GetLogical().createImageViewUnique(view_ci);

    // Create framebuffer
    framebuffer = device.GetLogical().createFramebufferUnique(
        vk::FramebufferCreateInfo({}, *renderpass, *image_view, extent.width, extent.height, 1));
}

} // namespace Vulkan