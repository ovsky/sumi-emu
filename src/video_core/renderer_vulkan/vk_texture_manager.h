// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <optional>

#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class MemoryAllocator;

// Enhanced texture manager for better error handling and thread safety
class TextureManager {
public:
    explicit TextureManager(const Device& device, MemoryAllocator& memory_allocator);
    ~TextureManager();

    // Get a texture from the cache, loading it if necessary
    VkImage GetTexture(const std::string& texture_path);

    // Force a texture to reload from disk
    void ReloadTexture(const std::string& texture_path);

    // Check if a texture is loaded correctly
    bool IsTextureLoadedCorrectly(VkImage texture);

    // Remove old textures from the cache
    void CleanupTextureCache();

    // Handle texture rendering, with automatic reload if needed
    void HandleTextureRendering(const std::string& texture_path,
                               std::function<void(VkImage)> render_callback);

private:
    // Load a texture from disk and create a Vulkan image
    vk::Image LoadTexture(const std::string& texture_path);

    // Create a default texture to use in case of errors
    vk::Image CreateDefaultTexture();

    const Device& device;
    MemoryAllocator& memory_allocator;
    std::mutex texture_mutex;
    std::unordered_map<std::string, vk::Image> texture_cache;
    std::optional<vk::Image> default_texture;
    VkFormat texture_format = VK_FORMAT_B8G8R8A8_SRGB;
};

} // namespace Vulkan