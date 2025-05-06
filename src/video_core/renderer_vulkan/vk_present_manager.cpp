// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/microprofile.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/frontend/emu_window.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

MICROPROFILE_DEFINE(Vulkan_WaitPresent, "Vulkan", "Wait For Present", MP_RGB(128, 128, 128));
MICROPROFILE_DEFINE(Vulkan_CopyToSwapchain, "Vulkan", "Copy to swapchain", MP_RGB(192, 255, 192));

namespace {

bool CanBlitToSwapchain(const vk::PhysicalDevice& physical_device, VkFormat format) {
    const VkFormatProperties props{physical_device.GetFormatProperties(format)};
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
}

// Add a helper function to convert VkResult to string
const char* VkResultToString(VkResult result) {
    switch (result) {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
        return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
        return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR:
        return "VK_SUBOPTIMAL_KHR";
    default:
        return "Unknown VkResult";
    }
}

[[nodiscard]] VkImageSubresourceLayers MakeImageSubresourceLayers() {
    return VkImageSubresourceLayers{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] VkImageBlit MakeImageBlit(s32 frame_width, s32 frame_height, s32 swapchain_width,
                                        s32 swapchain_height) {
    return VkImageBlit{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffsets =
            {
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                {
                    .x = frame_width,
                    .y = frame_height,
                    .z = 1,
                },
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffsets =
            {
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                {
                    .x = swapchain_width,
                    .y = swapchain_height,
                    .z = 1,
                },
            },
    };
}

[[nodiscard]] VkImageCopy MakeImageCopy(u32 frame_width, u32 frame_height, u32 swapchain_width,
                                        u32 swapchain_height) {
    return VkImageCopy{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffset =
            {
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffset =
            {
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .extent =
            {
                .width = std::min(frame_width, swapchain_width),
                .height = std::min(frame_height, swapchain_height),
                .depth = 1,
            },
    };
}

} // Anonymous namespace

PresentManager::PresentManager(const vk::Instance& instance_,
                               Core::Frontend::EmuWindow& render_window_, const Device& device_,
                               MemoryAllocator& memory_allocator_, Scheduler& scheduler_,
                               Swapchain& swapchain_, vk::SurfaceKHR& surface_)
    : instance{instance_}, render_window{render_window_}, device{device_},
      memory_allocator{memory_allocator_}, scheduler{scheduler_}, swapchain{swapchain_},
      surface{surface_}, blit_supported{CanBlitToSwapchain(device.GetPhysical(),
                                                           swapchain.GetImageViewFormat())},
      use_present_thread{Settings::values.async_presentation.GetValue()} {

    LOG_INFO(Render_Vulkan, "Initializing present manager. Async presentation: {}, Blit supported: {}",
             use_present_thread, blit_supported);

    SetImageCount();

    auto& dld = device.GetLogical();
    cmdpool = dld.CreateCommandPool({
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.GetGraphicsFamily(),
    });
    auto cmdbuffers = cmdpool.Allocate(image_count);

    frames.resize(image_count);
    for (u32 i = 0; i < frames.size(); i++) {
        Frame& frame = frames[i];
        frame.cmdbuf = vk::CommandBuffer{cmdbuffers[i], device.GetDispatchLoader()};
        frame.render_ready = dld.CreateSemaphore({
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
        });
        frame.present_done = dld.CreateFence({
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        });
        free_queue.push(&frame);
    }

    // Only create the present thread if async presentation is enabled and the device supports it
    if (use_present_thread) {
        try {
            present_thread = std::jthread([this](std::stop_token token) { PresentThread(token); });
            LOG_INFO(Render_Vulkan, "Presentation thread started successfully");
        } catch (const std::exception& e) {
            LOG_ERROR(Render_Vulkan, "Failed to start presentation thread: {}", e.what());
            use_present_thread = false;
        }
    }
}

PresentManager::~PresentManager() {
    // Ensure the present queue is empty before destroying it
    if (use_present_thread) {
        try {
            WaitPresent();
            // The thread will be automatically joined by std::jthread's destructor
            LOG_INFO(Render_Vulkan, "Presentation thread stopping");
        } catch (const std::exception& e) {
            LOG_ERROR(Render_Vulkan, "Error during present thread cleanup: {}", e.what());
        }
    }
}

Frame* PresentManager::GetRenderFrame() {
    MICROPROFILE_SCOPE(Vulkan_WaitPresent);

    // Wait for free presentation frames
    std::unique_lock<std::timed_mutex> lock{free_mutex};
    free_cv.wait(lock, [this] { return !free_queue.empty(); });

    // Take the frame from the queue
    Frame* frame = free_queue.front();
    free_queue.pop();

    // Wait for the presentation to be finished so all frame resources are free
    frame->present_done.Wait();
    frame->present_done.Reset();

    return frame;
}

void PresentManager::Present(Frame* frame) {
    if (!use_present_thread) {
        scheduler.WaitWorker();
        try {
            CopyToSwapchain(frame);
        } catch (const std::exception& e) {
            LOG_ERROR(Render_Vulkan, "Error during sync presentation: {}", e.what());
        }
        free_queue.push(frame);
        return;
    }

    // For asynchronous presentation, queue the frame and notify the present thread
    scheduler.Record([this, frame](vk::CommandBuffer) {
        // Try to acquire the queue mutex
        std::unique_lock<std::timed_mutex> lock{queue_mutex, std::defer_lock};
        if (!lock.try_lock()) {
            LOG_WARNING(Render_Vulkan, "Failed to acquire queue mutex in Present");
            // Push the frame back to the free queue if we can
            try {
                std::unique_lock<std::timed_mutex> free_lock{free_mutex, std::defer_lock};
                if (free_lock.try_lock()) {
                    free_queue.push(frame);
                    free_cv.notify_one();
                }
            } catch (...) {}
            return;
        }

        present_queue.push(frame);
        frame_cv.notify_one();
    });
}

void PresentManager::RecreateFrame(Frame* frame, u32 width, u32 height, VkFormat image_view_format,
                                   VkRenderPass rd) {
    auto& dld = device.GetLogical();

    frame->width = width;
    frame->height = height;

    frame->image = memory_allocator.CreateImage({
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = swapchain.GetImageFormat(),
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    });

    frame->image_view = dld.CreateImageView({
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = *frame->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_view_format,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    });

    const VkImageView image_view{*frame->image_view};
    frame->framebuffer = dld.CreateFramebuffer({
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = rd,
        .attachmentCount = 1,
        .pAttachments = &image_view,
        .width = width,
        .height = height,
        .layers = 1,
    });
}

void PresentManager::WaitPresent() {
    if (!use_present_thread) {
        return;
    }

    // Wait for the present queue to be empty with a timeout
    try {
        std::unique_lock queue_lock{queue_mutex};
        constexpr auto timeout = std::chrono::seconds(5);
        if (!frame_cv.wait_for(queue_lock, timeout, [this] { return present_queue.empty(); })) {
            LOG_WARNING(Render_Vulkan, "Timeout waiting for present queue to empty");
            return;
        }

        // The above condition will be satisfied when the last frame is taken from the queue.
        // To ensure that frame has been presented as well take hold of the swapchain
        // mutex with a timeout.
        if (!swapchain_mutex.try_lock()) {
            LOG_WARNING(Render_Vulkan, "Failed to acquire swapchain mutex");
            return;
        }
        swapchain_mutex.unlock();
    } catch (const std::exception& e) {
        LOG_ERROR(Render_Vulkan, "Exception during WaitPresent: {}", e.what());
    }
}

void PresentManager::PresentThread(std::stop_token token) {
    Common::SetCurrentThreadName("VulkanPresent");
    while (!token.stop_requested()) {
        try {
            std::unique_lock lock{queue_mutex};

            // Wait for presentation frames with a timeout to prevent hanging indefinitely
            constexpr auto timeout = std::chrono::milliseconds(500);
            bool has_frame = false;

            // Use wait_for with predicate instead of CondvarWait with timeout
            if (token.stop_requested()) {
                return;
            }

            has_frame = frame_cv.wait_for(lock, timeout, [this] {
                return !present_queue.empty();
            });

            if (token.stop_requested()) {
                return;
            }

            if (!has_frame) {
                // Timeout occurred, continue the loop
                continue;
            }

            if (present_queue.empty()) {
                // This shouldn't happen but handle it just in case
                continue;
            }

            // Take the frame and notify anyone waiting
            Frame* frame = present_queue.front();
            present_queue.pop();
            frame_cv.notify_one();

            // By exchanging the lock ownership we take the swapchain lock
            // before the queue lock goes out of scope. This way the swapchain
            // lock in WaitPresent is guaranteed to occur after here.
            std::unique_lock<std::mutex> swapchain_lock{swapchain_mutex, std::defer_lock};
            if (!swapchain_lock.try_lock()) {
                LOG_WARNING(Render_Vulkan, "Failed to acquire swapchain lock, skipping presentation");
                // Return the frame to the free queue even if we couldn't present it
                std::scoped_lock fl{free_mutex};
                free_queue.push(frame);
                free_cv.notify_one();
                continue;
            }

            lock.unlock(); // Release queue lock after acquiring swapchain lock

            try {
                CopyToSwapchain(frame);
            } catch (const std::exception& e) {
                LOG_ERROR(Render_Vulkan, "Error during presentation: {}", e.what());
            }

            // Free the frame for reuse
            std::scoped_lock fl{free_mutex};
            free_queue.push(frame);
            free_cv.notify_one();
        } catch (const std::exception& e) {
            LOG_ERROR(Render_Vulkan, "Exception in present thread: {}", e.what());
            // Continue the loop instead of letting the thread die
        }
    }
}

void PresentManager::RecreateSwapchain(Frame* frame) {
    swapchain.Create(*surface, frame->width, frame->height);
    SetImageCount();
}

void PresentManager::SetImageCount() {
    // We cannot have more than 7 images in flight at any given time.
    // FRAMES_IN_FLIGHT is 8, and the cache TICKS_TO_DESTROY is 8.
    // Mali drivers will give us 6.
    image_count = std::min<size_t>(swapchain.GetImageCount(), 7);
}

void PresentManager::CopyToSwapchain(Frame* frame) {
    bool requires_recreation = false;
    u32 recreation_attempts = 0;
    const u32 max_recreation_attempts = 3;

    while (recreation_attempts < max_recreation_attempts) {
        try {
            // Recreate surface and swapchain if needed.
            if (requires_recreation) {
                LOG_INFO(Render_Vulkan, "Recreating swapchain (attempt {}/{})",
                         recreation_attempts + 1, max_recreation_attempts);
                surface = CreateSurface(instance, render_window.GetWindowInfo());
                RecreateSwapchain(frame);
                recreation_attempts++;
            }

            // Draw to swapchain.
            return CopyToSwapchainImpl(frame);
        } catch (const vk::Exception& except) {
            const VkResult result = except.GetResult();
            if (result != VK_ERROR_SURFACE_LOST_KHR &&
                result != VK_ERROR_OUT_OF_DATE_KHR &&
                result != VK_SUBOPTIMAL_KHR) {
                LOG_ERROR(Render_Vulkan, "Swapchain presentation failed with error {}: {}",
                          result, VkResultToString(result));
                throw;
            }

            LOG_WARNING(Render_Vulkan, "Swapchain lost or outdated with error {}: {}, recreating",
                        result, VkResultToString(result));
            requires_recreation = true;
            recreation_attempts++;
        }
    }

    LOG_ERROR(Render_Vulkan, "Failed to present after {} recreation attempts", max_recreation_attempts);
}

void PresentManager::CopyToSwapchainImpl(Frame* frame) {
    MICROPROFILE_SCOPE(Vulkan_CopyToSwapchain);

    // If the size of the incoming frames has changed, recreate the swapchain
    // to account for that.
    const bool is_suboptimal = swapchain.NeedsRecreation();
    const bool size_changed =
        swapchain.GetWidth() != frame->width || swapchain.GetHeight() != frame->height;
    if (is_suboptimal || size_changed) {
        RecreateSwapchain(frame);
    }

    while (swapchain.AcquireNextImage()) {
        RecreateSwapchain(frame);
    }

    const vk::CommandBuffer cmdbuf{frame->cmdbuf};
    cmdbuf.Begin({
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    });

    const VkImage image{swapchain.CurrentImage()};
    const VkExtent2D extent = swapchain.GetExtent();
    const std::array pre_barriers{
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *frame->image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
    };
    const std::array post_barriers{
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
        VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *frame->image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
    };

    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {},
                           {}, {}, pre_barriers);

    if (blit_supported) {
        cmdbuf.BlitImage(*frame->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         MakeImageBlit(frame->width, frame->height, extent.width, extent.height),
                         VK_FILTER_LINEAR);
    } else {
        cmdbuf.CopyImage(*frame->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         MakeImageCopy(frame->width, frame->height, extent.width, extent.height));
    }

    cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, {},
                           {}, {}, post_barriers);

    cmdbuf.End();

    const VkSemaphore present_semaphore = swapchain.CurrentPresentSemaphore();
    const VkSemaphore render_semaphore = swapchain.CurrentRenderSemaphore();
    const std::array wait_semaphores = {present_semaphore, *frame->render_ready};

    static constexpr std::array<VkPipelineStageFlags, 2> wait_stage_masks{
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
    };

    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 2U,
        .pWaitSemaphores = wait_semaphores.data(),
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = cmdbuf.address(),
        .signalSemaphoreCount = 1U,
        .pSignalSemaphores = &render_semaphore,
    };

    // Submit the image copy/blit to the swapchain
    {
        std::scoped_lock submit_lock{scheduler.submit_mutex};
        switch (const VkResult result =
                    device.GetGraphicsQueue().Submit(submit_info, *frame->present_done)) {
        case VK_SUCCESS:
            break;
        case VK_ERROR_DEVICE_LOST:
            device.ReportLoss();
            [[fallthrough]];
        default:
            vk::Check(result);
            break;
        }
    }

    // Present
    swapchain.Present(render_semaphore);
}

} // namespace Vulkan
