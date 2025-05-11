// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_texture_manager.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

TextureManager::TextureManager(const Device& device_, MemoryAllocator& memory_allocator_)
    : device(device_), memory_allocator(memory_allocator_) {

    // Create a default texture for fallback in case of errors
    default_texture = CreateDefaultTexture();
}

TextureManager::~TextureManager() {
    std::lock_guard<std::mutex> lock(texture_mutex);
    // Clear all cached textures
    texture_cache.clear();

    // Default texture will be cleaned up automatically by vk::Image's destructor
}

VkImage TextureManager::GetTexture(const std::string& texture_path) {
    std::lock_guard<std::mutex> lock(texture_mutex);

    // Check if the texture is already in the cache
    auto it = texture_cache.find(texture_path);
    if (it != texture_cache.end()) {
        return *it->second;
    }

    // Load the texture and add it to the cache
    vk::Image new_texture = LoadTexture(texture_path);
    if (new_texture) {
        VkImage raw_handle = *new_texture;
        texture_cache.emplace(texture_path, std::move(new_texture));
        return raw_handle;
    }

    // If loading fails, return the default texture if it exists
    LOG_WARNING(Render_Vulkan, "Failed to load texture: {}, using default", texture_path);
    if (default_texture.has_value()) {
        return *(*default_texture);
    }
    return VK_NULL_HANDLE;
}

void TextureManager::ReloadTexture(const std::string& texture_path) {
    std::lock_guard<std::mutex> lock(texture_mutex);

    // Remove the texture from cache if it exists
    auto it = texture_cache.find(texture_path);
    if (it != texture_cache.end()) {
        LOG_INFO(Render_Vulkan, "Reloading texture: {}", texture_path);
        texture_cache.erase(it);
    }

    // The texture will be reloaded on next GetTexture call
}

bool TextureManager::IsTextureLoadedCorrectly(VkImage texture) {
    // Check if the texture handle is valid
    static const VkImage null_handle = VK_NULL_HANDLE;
    return texture != null_handle;
}

void TextureManager::CleanupTextureCache() {
    std::lock_guard<std::mutex> lock(texture_mutex);

    // TODO: track usage and remove unused textures [ZEP]

    LOG_INFO(Render_Vulkan, "Handling texture cache cleanup, current size: {}", texture_cache.size());
}

void TextureManager::HandleTextureRendering(const std::string& texture_path,
                                          std::function<void(VkImage)> render_callback) {
    VkImage texture = GetTexture(texture_path);

    if (!IsTextureLoadedCorrectly(texture)) {
        LOG_ERROR(Render_Vulkan, "Texture failed to load correctly: {}, attempting reload", texture_path);
        ReloadTexture(texture_path);
        texture = GetTexture(texture_path);
    }

    // Execute the rendering callback with the texture
    render_callback(texture);
}

vk::Image TextureManager::LoadTexture(const std::string& texture_path) {
    // TODO: load image data from disk
    // and create a proper Vulkan texture [ZEP]

    if (!std::filesystem::exists(texture_path)) {
        LOG_ERROR(Render_Vulkan, "Texture file not found: {}", texture_path);
        return {};
    }

    try {

        LOG_INFO(Render_Vulkan, "Loaded texture: {}", texture_path);

        // TODO: create an actual VkImage [ZEP]
        return CreateDefaultTexture();
    } catch (const std::exception& e) {
        LOG_ERROR(Render_Vulkan, "Error loading texture {}: {}", texture_path, e.what());
        return {};
    }
}

vk::Image TextureManager::CreateDefaultTexture() {
    // Create a small default texture (1x1 pixel) to use as a fallback
//    const VkExtent2D extent{1, 1};

/*     // Create image
    const VkImageCreateInfo image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = texture_format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    }; */

    // TODO: create an actual VkImage [ZEP]
    LOG_INFO(Render_Vulkan, "Created default fallback texture");
    return {};
}

} // namespace Vulkan