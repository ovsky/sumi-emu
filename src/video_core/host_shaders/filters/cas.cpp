#include "cas.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include <vulkan/vulkan.hpp> // Include Vulkan-Hpp for VULKAN_HPP_DEFAULT_DISPATCHER
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

    CAS::CAS(const Device& device_, MemoryAllocator& allocator_, Scheduler& scheduler_,
             DescriptorPool& descriptor_pool_)
            : device(device_), allocator(allocator_), scheduler(scheduler_),
              descriptor_pool(descriptor_pool_) {
        CreateRenderPass();
        CreateDescriptorSetLayout();
        CreatePipeline();
    }

    CAS::~CAS() {
        // const auto& dld = device.GetDispatchLoader();
        device.GetLogical().WaitIdle();
    }

    void CAS::SetSharpness(float sharpness_) {
        sharpness = sharpness_;
    }

    void CAS::SetExtent(VideoCommon::Extent2D new_extent) {
        if (extent.width != new_extent.width || extent.height != new_extent.height) {
            extent = new_extent;
            CreateFramebuffer();
        }
    }

    void CAS::Draw(const TextureCache& texture_cache, vk::ImageView input_image_view, VideoCommon::Extent2D draw_extent) {
        vk::CommandBuffer cmdbuf;
        scheduler.Record([&](vk::CommandBuffer command_buffer) {
            cmdbuf = command_buffer;
        });

        // const vk::DispatchLoaderDynamic& dld = device.GetDispatchLoader();
        const auto& dld = VULKAN_HPP_DEFAULT_DISPATCHER;

        VkDescriptorImageInfo image_info{texture_cache.GetSampler(), input_image_view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        WriteDescriptorSet descriptor_write{descriptor_set, 0, 0,
                                                vk::DescriptorType::eCombinedImageSampler,
                                                &image_info};
        device.GetLogical().updateDescriptorSets(1, &descriptor_write, 0, nullptr, dld);

        vk::RenderPassBeginInfo rp_bi;
        rp_bi.renderPass = *renderpass;
        rp_bi.framebuffer = *framebuffer;
        rp_bi.renderArea.extent = extent;

        std::array<vk::ClearValue, 1> clear_values{};
        clear_values[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
        rp_bi.clearValueCount = static_cast<uint32_t>(clear_values.size());
        rp_bi.pClearValues = clear_values.data();

        cmdbuf.beginRenderPass(rp_bi, vk::SubpassContents::eInline, dld);

        vk::Viewport viewport(0.0f, 0.0f,
                              static_cast<float>(extent.width),
                              static_cast<float>(extent.height),
                              0.0f, 1.0f);
        vk::Rect2D scissor({0, 0}, extent);
        cmdbuf.setViewport(0, viewport, dld);
        cmdbuf.setScissor(0, scissor, dld);

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline, dld);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layout,
                                  0, descriptor_set, {}, dld);

        struct PushConstants {
            float sharpness;
        } constants{sharpness};
        cmdbuf.pushConstants<PushConstants>(*layout,
                                            vk::ShaderStageFlagBits::eFragment,
                                            0, constants, dld);

        cmdbuf.draw(3, 1, 0, 0, dld);

        cmdbuf.endRenderPass(dld);

        vk::ImageMemoryBarrier barrier;
        barrier.image = *image;
        barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        cmdbuf.pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eFragmentShader,
                vk::DependencyFlags(),
                0, nullptr,
                0, nullptr,
                1, &barrier,
                dld);
    }

    void CAS::CreateRenderPass() {
        vk::AttachmentDescription attachment;
        attachment.format = vk::Format::eB8G8R8A8Unorm;
        attachment.samples = vk::SampleCountFlagBits::e1;
        attachment.loadOp = vk::AttachmentLoadOp::eClear;
        attachment.storeOp = vk::AttachmentStoreOp::eStore;
        attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachment.initialLayout = vk::ImageLayout::eUndefined;
        attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference color_attachment(0, vk::ImageLayout::eColorAttachmentOptimal);
        vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, color_attachment);

        renderpass = device.GetLogical().createRenderPassUnique(
                vk::RenderPassCreateInfo({}, attachment, subpass), nullptr, device.GetDispatchLoader());
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
        const auto vert_code = CompileShader(ReadFile(VK_SHADERS_DIR "filters/full_screen.vert"),
        vk::ShaderStageFlagBits::eVertex);
        const auto frag_code = CompileShader(ReadFile(VK_SHADERS_DIR "filters/cas.frag"),
        vk::ShaderStageFlagBits::eFragment);

        const auto vert_module = device.GetLogical().createShaderModuleUnique(
                vk::ShaderModuleCreateInfo({}, vert_code.size(), reinterpret_cast<const u32*>(vert_code.data())),
                nullptr, device.GetDispatchLoader());
        const auto frag_module = device.GetLogical().createShaderModuleUnique(
                vk::ShaderModuleCreateInfo({}, frag_code.size(), reinterpret_cast<const u32*>(frag_code.data())),
                nullptr, device.GetDispatchLoader());

        const std::array stages = {
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex,
                                                  *vert_module, "main"),
                vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment,
                                                  *frag_module, "main"),
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

    void CAS::CreateFramebuffer() {
        const auto& dld = device.GetDispatchLoader();

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

        image = device.GetLogical().createImageUnique(image_ci, nullptr, dld);

        vk::MemoryRequirements mem_reqs = device.GetLogical().getImageMemoryRequirements(*image, dld);
        allocation = allocator.Commit(mem_reqs, MemoryUsage::DeviceLocal);

        device.GetLogical().bindImageMemory(*image, allocation.memory, allocation.offset, dld);

        vk::ImageViewCreateInfo view_ci;
        view_ci.image = *image;
        view_ci.viewType = vk::ImageViewType::e2D;
        view_ci.format = vk::Format::eB8G8R8A8Unorm;
        view_ci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.layerCount = 1;

        image_view = device.GetLogical().createImageViewUnique(view_ci, nullptr, dld);

        framebuffer = device.GetLogical().createFramebufferUnique(
                vk::FramebufferCreateInfo({}, *renderpass, *image_view, extent.width, extent.height, 1),
                nullptr, dld);
    }

} // namespace Vulkan