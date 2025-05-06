// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

// Priority constants for shader compilation
extern const u32 SHADER_PRIORITY_CRITICAL;
extern const u32 SHADER_PRIORITY_HIGH;
extern const u32 SHADER_PRIORITY_NORMAL;
extern const u32 SHADER_PRIORITY_LOW;

// Command queue system for asynchronous operations
void InitializeCommandQueue();
void ShutdownCommandQueue();
void SubmitCommandToQueue(std::function<void()> command);
void CommandQueueWorker();

// Thread pool management for shader compilation
void InitializeThreadPool();
void ShutdownThreadPool();
void SubmitShaderCompilationTask(std::function<void()> task, u32 priority);
float GetShaderCompilationProgress();
bool IsShaderCompilationInProgress();

// Predictive shader loading
void PredictShader(const std::string& shader_path);
void PreloadPredictedShaders(const Device& device);

// Scheduler integration functions
void SetGlobalScheduler(Scheduler* scheduler);
void SubmitToScheduler(std::function<void(vk::CommandBuffer)> command);
u64 FlushScheduler(VkSemaphore signal_semaphore = nullptr, VkSemaphore wait_semaphore = nullptr);
void ProcessAllCommands();

vk::ShaderModule BuildShader(const Device& device, std::span<const u32> code);

// Enhanced shader functionality
bool IsShaderValid(VkShaderModule shader_module);

void AsyncCompileShader(const Device& device, const std::string& shader_path,
    std::function<void(VkShaderModule)> callback, u32 priority);

void AsyncCompileShader(const Device& device, const std::string& shader_path,
                       std::function<void(VkShaderModule)> callback);

class ShaderManager {
public:
    explicit ShaderManager(const Device& device);
    ~ShaderManager();

    VkShaderModule GetShaderModule(const std::string& shader_path);
    void ReloadShader(const std::string& shader_path);
    bool LoadShader(const std::string& shader_path);
    void WaitForCompilation();

    // Enhanced shader management
    void BatchLoadShaders(const std::vector<std::string>& shader_paths,
    const std::vector<u32>& priorities);
    void PreloadShaderDirectory(const std::string& directory_path);
    float GetCompilationProgress() const;

    // Batch process multiple shaders in parallel
    void PreloadShaders(const std::vector<std::string>& shader_paths);

    // Integrate with Sumi's scheduler
    void SetScheduler(Scheduler* scheduler);

private:
    const Device& device;
    std::mutex shader_mutex;
    std::unordered_map<std::string, vk::ShaderModule> shader_cache;
};

} // namespace Vulkan
