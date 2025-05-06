// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 sumi + citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <thread>
#include <filesystem>
#include <fstream>
#include <vector>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <future>
#include <chrono>
#include <unordered_set>

#include "common/common_types.h"
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

#define SHADER_CACHE_DIR "./shader_cache"

namespace Vulkan {

// Global command submission queue for asynchronous operations
std::mutex commandQueueMutex;
std::queue<std::function<void()>> commandQueue;
std::condition_variable commandQueueCondition;
std::atomic<bool> isCommandQueueActive{true};
std::thread commandQueueThread;

// Pointer to sumi's scheduler for integration
Scheduler* globalScheduler = nullptr;

// Constants for thread pool and shader management
constexpr size_t DEFAULT_THREAD_POOL_SIZE = 4;
constexpr size_t MAX_THREAD_POOL_SIZE = 8;
constexpr u32 SHADER_PRIORITY_CRITICAL = 0;
constexpr u32 SHADER_PRIORITY_HIGH = 1;
constexpr u32 SHADER_PRIORITY_NORMAL = 2;
constexpr u32 SHADER_PRIORITY_LOW = 3;

// Thread pool for shader compilation
std::vector<std::thread> g_thread_pool;
std::queue<std::function<void()>> g_work_queue;
std::mutex g_work_queue_mutex;
std::condition_variable g_work_queue_cv;
std::atomic<bool> g_thread_pool_initialized = false;
std::atomic<bool> g_shutdown_thread_pool = false;
std::atomic<size_t> g_active_compilation_tasks = 0;
std::atomic<size_t> g_total_compilation_tasks = 0;
std::atomic<size_t> g_completed_compilation_tasks = 0;

// Priority queue for shader compilation
struct ShaderCompilationTask {
    std::function<void()> task;
    u32 priority;
    std::chrono::high_resolution_clock::time_point enqueue_time;

    bool operator<(const ShaderCompilationTask& other) const {
        // Lower priority value means higher actual priority
        if (priority != other.priority) {
            return priority > other.priority;
        }
        // If priorities are equal, use FIFO ordering
        return enqueue_time > other.enqueue_time;
    }
};
std::priority_queue<ShaderCompilationTask> g_priority_work_queue;

// Predictive shader loading
std::unordered_set<std::string> g_predicted_shaders;
std::mutex g_predicted_shaders_mutex;

// Command queue worker thread (multi-threaded command recording)
void CommandQueueWorker() {
    while (isCommandQueueActive.load()) {
        std::function<void()> command;
        {
            std::unique_lock<std::mutex> lock(commandQueueMutex);
            if (commandQueue.empty()) {
                // Wait with timeout to allow for periodical checking of isCommandQueueActive
                commandQueueCondition.wait_for(lock, std::chrono::milliseconds(100),
                    []{ return !commandQueue.empty() || !isCommandQueueActive.load(); });

                // If we woke up but the queue is still empty and we should still be active, loop
                if (commandQueue.empty()) {
                    continue;
                }
            }

            command = commandQueue.front();
            commandQueue.pop();
        }

        // Execute the command
        if (command) {
            command();
        }
    }
}

// Initialize the command queue system
void InitializeCommandQueue() {
    if (!commandQueueThread.joinable()) {
        isCommandQueueActive.store(true);
        commandQueueThread = std::thread(CommandQueueWorker);
    }
}

// Shutdown the command queue system
void ShutdownCommandQueue() {
    isCommandQueueActive.store(false);
    commandQueueCondition.notify_all();

    if (commandQueueThread.joinable()) {
        commandQueueThread.join();
    }
}

// Submit a command to the queue for asynchronous execution
void SubmitCommandToQueue(std::function<void()> command) {
    {
        std::lock_guard<std::mutex> lock(commandQueueMutex);
        commandQueue.push(command);
    }
    commandQueueCondition.notify_one();
}

// Set the global scheduler reference for command integration
void SetGlobalScheduler(Scheduler* scheduler) {
    globalScheduler = scheduler;
}

// Submit a Vulkan command to the existing sumi scheduler
void SubmitToScheduler(std::function<void(vk::CommandBuffer)> command) {
    if (globalScheduler) {
        globalScheduler->Record(std::move(command));
    } else {
        LOG_WARNING(Render_Vulkan, "Trying to submit to scheduler but no scheduler is set");
    }
}

// Flush the sumi scheduler - use when needing to ensure commands are executed
u64 FlushScheduler(VkSemaphore signal_semaphore, VkSemaphore wait_semaphore) {
    if (globalScheduler) {
        return globalScheduler->Flush(signal_semaphore, wait_semaphore);
    } else {
        LOG_WARNING(Render_Vulkan, "Trying to flush scheduler but no scheduler is set");
        return 0;
    }
}

// Process both command queue and scheduler commands
void ProcessAllCommands() {
    // Process our command queue first
    {
        std::unique_lock<std::mutex> lock(commandQueueMutex);
        while (!commandQueue.empty()) {
            auto command = commandQueue.front();
            commandQueue.pop();
            lock.unlock();

            command();

            lock.lock();
        }
    }

    // Then flush the scheduler if it exists
    if (globalScheduler) {
        globalScheduler->Flush();
    }
}

vk::ShaderModule BuildShader(const Device& device, std::span<const u32> code) {
    return device.GetLogical().CreateShaderModule({
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = static_cast<u32>(code.size_bytes()),
        .pCode = code.data(),
    });
}

bool IsShaderValid(VkShaderModule shader_module) {
    // TODO: validate the shader by checking if it's null
    // or by examining SPIR-V data for correctness [ZEP]
    return shader_module != VK_NULL_HANDLE;
}

// Initialize thread pool for shader compilation
void InitializeThreadPool() {
    if (g_thread_pool_initialized) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_work_queue_mutex);
    g_shutdown_thread_pool = false;

    // Determine optimal thread count based on system
    const size_t hardware_threads = std::max(std::thread::hardware_concurrency(), 2u);
    const size_t thread_count = std::min(hardware_threads - 1, MAX_THREAD_POOL_SIZE);

    LOG_INFO(Render_Vulkan, "Initializing shader compilation thread pool with {} threads", thread_count);

    for (size_t i = 0; i < thread_count; ++i) {
        g_thread_pool.emplace_back([]() {
            while (!g_shutdown_thread_pool) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> thread_pool_lock(g_work_queue_mutex);
                    g_work_queue_cv.wait(thread_pool_lock, [] {
                        return g_shutdown_thread_pool || !g_priority_work_queue.empty();
                    });

                    if (g_shutdown_thread_pool && g_priority_work_queue.empty()) {
                        break;
                    }

                    if (!g_priority_work_queue.empty()) {
                        ShaderCompilationTask highest_priority_task = g_priority_work_queue.top();
                        g_priority_work_queue.pop();
                        task = std::move(highest_priority_task.task);
                    }
                }

                if (task) {
                    g_active_compilation_tasks++;
                    task();
                    g_active_compilation_tasks--;
                    g_completed_compilation_tasks++;
                }
            }
        });
    }

    g_thread_pool_initialized = true;
}

// Shutdown thread pool
void ShutdownThreadPool() {
    if (!g_thread_pool_initialized) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_work_queue_mutex);
        g_shutdown_thread_pool = true;
    }

    g_work_queue_cv.notify_all();

    for (auto& thread : g_thread_pool) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    g_thread_pool.clear();
    g_thread_pool_initialized = false;

    LOG_INFO(Render_Vulkan, "Shader compilation thread pool shutdown");
}

// Submit work to thread pool with priority
void SubmitShaderCompilationTask(std::function<void()> task, u32 priority) {
    if (!g_thread_pool_initialized) {
        InitializeThreadPool();
    }

    {
        std::lock_guard<std::mutex> work_queue_lock(g_work_queue_mutex);
        g_priority_work_queue.push({
            std::move(task),
            priority,
            std::chrono::high_resolution_clock::now()
        });
        g_total_compilation_tasks++;
    }

    g_work_queue_cv.notify_one();
}

// Get shader compilation progress (0.0f - 1.0f)
float GetShaderCompilationProgress() {
    const size_t total = g_total_compilation_tasks.load();
    if (total == 0) {
        return 1.0f;
    }

    const size_t completed = g_completed_compilation_tasks.load();
    return static_cast<float>(completed) / static_cast<float>(total);
}

// Check if any shader compilation is in progress
bool IsShaderCompilationInProgress() {
    return g_active_compilation_tasks.load() > 0;
}

// Add shader to prediction list for preloading
void PredictShader(const std::string& shader_path) {
    std::lock_guard<std::mutex> lock(g_predicted_shaders_mutex);
    g_predicted_shaders.insert(shader_path);
}

// Preload predicted shaders
void PreloadPredictedShaders(const Device& device) {
    std::unordered_set<std::string> shaders_to_load;
    {
        std::lock_guard<std::mutex> lock(g_predicted_shaders_mutex);
        shaders_to_load = g_predicted_shaders;
        g_predicted_shaders.clear();
    }

    if (shaders_to_load.empty()) {
        return;
    }

    LOG_INFO(Render_Vulkan, "Preloading {} predicted shaders", shaders_to_load.size());

    for (const auto& shader_path : shaders_to_load) {
        // Queue with low priority since these are predictions
        AsyncCompileShader(device, shader_path, [](VkShaderModule) {}, SHADER_PRIORITY_LOW);
    }
}

// Atomic flag for tracking shader compilation status
std::atomic<bool> compilingShader(false);

void AsyncCompileShader(const Device& device, const std::string& shader_path,
                       std::function<void(VkShaderModule)> callback, u32 priority) {
    LOG_INFO(Render_Vulkan, "Asynchronously compiling shader: {}", shader_path);

    // Create shader cache directory if it doesn't exist
    if (!std::filesystem::exists(SHADER_CACHE_DIR)) {
        std::filesystem::create_directory(SHADER_CACHE_DIR);
    }

    // Initialize thread pool if needed
    if (!g_thread_pool_initialized) {
        InitializeThreadPool();
    }

    // Submit to thread pool with priority
    SubmitShaderCompilationTask([device_ptr = &device, shader_path, callback = std::move(callback)]() {
        auto startTime = std::chrono::high_resolution_clock::now();

        try {
            std::vector<u32> spir_v;
            bool success = false;

            // Check if the file exists and attempt to read it
            if (std::filesystem::exists(shader_path)) {
                std::ifstream shader_file(shader_path, std::ios::binary);
                if (shader_file) {
                    shader_file.seekg(0, std::ios::end);
                    size_t file_size = static_cast<size_t>(shader_file.tellg());
                    shader_file.seekg(0, std::ios::beg);

                    spir_v.resize(file_size / sizeof(u32));
                    if (shader_file.read(reinterpret_cast<char*>(spir_v.data()), file_size)) {
                        success = true;
                    }
                }
            }

            if (success) {
                vk::ShaderModule shader = BuildShader(*device_ptr, spir_v);
                if (IsShaderValid(*shader)) {
                    // Cache the compiled shader to disk for faster loading next time
                    std::string cache_path = std::string(SHADER_CACHE_DIR) + "/" +
                                           std::filesystem::path(shader_path).filename().string() + ".cache";

                    std::ofstream cache_file(cache_path, std::ios::binary);
                    if (cache_file) {
                        cache_file.write(reinterpret_cast<const char*>(spir_v.data()),
                                        spir_v.size() * sizeof(u32));
                    }

                    auto endTime = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double> duration = endTime - startTime;
                    LOG_INFO(Render_Vulkan, "Shader compiled in {:.2f} seconds: {}",
                            duration.count(), shader_path);

                    // Store the module pointer for the callback
                    VkShaderModule raw_module = *shader;

                    // Submit callback to main thread via command queue for thread safety
                    SubmitCommandToQueue([callback = std::move(callback), raw_module]() {
                        callback(raw_module);
                    });
                } else {
                    LOG_ERROR(Render_Vulkan, "Shader validation failed: {}", shader_path);
                    SubmitCommandToQueue([callback = std::move(callback)]() {
                        callback(VK_NULL_HANDLE);
                    });
                }
            } else {
                LOG_ERROR(Render_Vulkan, "Failed to read shader file: {}", shader_path);
                SubmitCommandToQueue([callback = std::move(callback)]() {
                    callback(VK_NULL_HANDLE);
                });
            }
        } catch (const std::exception& e) {
            LOG_ERROR(Render_Vulkan, "Error compiling shader: {}", e.what());
            SubmitCommandToQueue([callback = std::move(callback)]() {
                callback(VK_NULL_HANDLE);
            });
        }
    }, priority);
}

// Overload for backward compatibility
void AsyncCompileShader(const Device& device, const std::string& shader_path,
                       std::function<void(VkShaderModule)> callback) {
    AsyncCompileShader(device, shader_path, std::move(callback), SHADER_PRIORITY_NORMAL);
}

ShaderManager::ShaderManager(const Device& device_) : device(device_) {
    // Initialize command queue system
    InitializeCommandQueue();

    // Initialize thread pool for shader compilation
    InitializeThreadPool();
}

ShaderManager::~ShaderManager() {
    // Wait for any pending compilations to finish
    WaitForCompilation();

    // Clean up shader modules
    std::lock_guard<std::mutex> lock(shader_mutex);
    shader_cache.clear();

    // Shutdown thread pool
    ShutdownThreadPool();

    // Shutdown command queue
    ShutdownCommandQueue();
}

VkShaderModule ShaderManager::GetShaderModule(const std::string& shader_path) {
    // Check in-memory cache first
    {
        std::lock_guard<std::mutex> lock(shader_mutex);
        auto it = shader_cache.find(shader_path);
        if (it != shader_cache.end()) {
            return *it->second;
        }
    }

    // Normalize the path to avoid filesystem issues
    std::string normalized_path = shader_path;
    std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

    // Check if shader exists
    if (!std::filesystem::exists(normalized_path)) {
        LOG_WARNING(Render_Vulkan, "Shader file does not exist: {}", normalized_path);
        return VK_NULL_HANDLE;
    }

    // Check if shader is available in disk cache first
    const std::string filename = std::filesystem::path(normalized_path).filename().string();
    std::string cache_path = std::string(SHADER_CACHE_DIR) + "/" + filename + ".cache";

    if (std::filesystem::exists(cache_path)) {
        try {
            // Load the cached shader
            std::ifstream cache_file(cache_path, std::ios::binary);
            if (cache_file) {
                cache_file.seekg(0, std::ios::end);
                size_t file_size = static_cast<size_t>(cache_file.tellg());

                if (file_size > 0 && file_size % sizeof(u32) == 0) {
                    cache_file.seekg(0, std::ios::beg);
                    std::vector<u32> spir_v;
                    spir_v.resize(file_size / sizeof(u32));

                    if (cache_file.read(reinterpret_cast<char*>(spir_v.data()), file_size)) {
                        vk::ShaderModule shader = BuildShader(device, spir_v);
                        if (IsShaderValid(*shader)) {
                            // Store in memory cache
                            std::lock_guard<std::mutex> lock(shader_mutex);
                            shader_cache[normalized_path] = std::move(shader);
                            LOG_INFO(Render_Vulkan, "Loaded shader from cache: {}", normalized_path);
                            return *shader_cache[normalized_path];
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_WARNING(Render_Vulkan, "Failed to load shader from cache: {}", e.what());
            // Continue to load from original file
        }
    }

    // Try to load the shader directly if cache load failed
    if (LoadShader(normalized_path)) {
        std::lock_guard<std::mutex> lock(shader_mutex);
        return *shader_cache[normalized_path];
    }

    LOG_ERROR(Render_Vulkan, "Failed to load shader: {}", normalized_path);
    return VK_NULL_HANDLE;
}

void ShaderManager::ReloadShader(const std::string& shader_path) {
    LOG_INFO(Render_Vulkan, "Reloading shader: {}", shader_path);

    // Remove the old shader from cache
    {
        std::lock_guard<std::mutex> lock(shader_mutex);
        shader_cache.erase(shader_path);
    }

    // Load the shader again
    LoadShader(shader_path);
}

bool ShaderManager::LoadShader(const std::string& shader_path) {
    LOG_INFO(Render_Vulkan, "Loading shader from: {}", shader_path);

    if (!std::filesystem::exists(shader_path)) {
        LOG_ERROR(Render_Vulkan, "Shader file does not exist: {}", shader_path);
        return false;
    }

    try {
        std::vector<u32> spir_v;
        std::ifstream shader_file(shader_path, std::ios::binary);

        if (!shader_file.is_open()) {
            LOG_ERROR(Render_Vulkan, "Failed to open shader file: {}", shader_path);
            return false;
        }

        shader_file.seekg(0, std::ios::end);
        const size_t file_size = static_cast<size_t>(shader_file.tellg());

        if (file_size == 0 || file_size % sizeof(u32) != 0) {
            LOG_ERROR(Render_Vulkan, "Invalid shader file size ({}): {}", file_size, shader_path);
            return false;
        }

        shader_file.seekg(0, std::ios::beg);
        spir_v.resize(file_size / sizeof(u32));

        if (!shader_file.read(reinterpret_cast<char*>(spir_v.data()), file_size)) {
            LOG_ERROR(Render_Vulkan, "Failed to read shader data: {}", shader_path);
            return false;
        }

        vk::ShaderModule shader = BuildShader(device, spir_v);
        if (!IsShaderValid(*shader)) {
            LOG_ERROR(Render_Vulkan, "Created shader module is invalid: {}", shader_path);
            return false;
        }

        // Store in memory cache
        {
            std::lock_guard<std::mutex> lock(shader_mutex);
            shader_cache[shader_path] = std::move(shader);
        }

        // Also store in disk cache for future use
        try {
            if (!std::filesystem::exists(SHADER_CACHE_DIR)) {
                std::filesystem::create_directory(SHADER_CACHE_DIR);
            }

            std::string cache_path = std::string(SHADER_CACHE_DIR) + "/" +
                                  std::filesystem::path(shader_path).filename().string() + ".cache";

            std::ofstream cache_file(cache_path, std::ios::binary);
            if (cache_file.is_open()) {
                cache_file.write(reinterpret_cast<const char*>(spir_v.data()),
                                spir_v.size() * sizeof(u32));

                if (!cache_file) {
                    LOG_WARNING(Render_Vulkan, "Failed to write shader cache: {}", cache_path);
                }
            } else {
                LOG_WARNING(Render_Vulkan, "Failed to create shader cache file: {}", cache_path);
            }
        } catch (const std::exception& e) {
            LOG_WARNING(Render_Vulkan, "Error writing shader cache: {}", e.what());
            // Continue even if disk cache fails
        }

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Render_Vulkan, "Error loading shader: {}", e.what());
        return false;
    }
}

void ShaderManager::WaitForCompilation() {
    // Wait until no shader is being compiled
    while (IsShaderCompilationInProgress()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Process any pending commands in the queue
    std::unique_lock<std::mutex> lock(commandQueueMutex);
    while (!commandQueue.empty()) {
        auto command = commandQueue.front();
        commandQueue.pop();
        lock.unlock();

        command();

        lock.lock();
    }
}

// Integrate with sumi's scheduler for shader operations
void ShaderManager::SetScheduler(Scheduler* scheduler) {
    SetGlobalScheduler(scheduler);
}

// Load multiple shaders in parallel
void ShaderManager::PreloadShaders(const std::vector<std::string>& shader_paths) {
    if (shader_paths.empty()) {
        return;
    }

    LOG_INFO(Render_Vulkan, "Preloading {} shaders", shader_paths.size());

    // Track shaders that need to be loaded
    std::unordered_set<std::string> shaders_to_load;

    // First check which shaders are not already cached
    {
        std::lock_guard<std::mutex> lock(shader_mutex);
        for (const auto& path : shader_paths) {
            if (shader_cache.find(path) == shader_cache.end()) {
                // Also check disk cache
                if (std::filesystem::exists(path)) {
                    std::string cache_path = std::string(SHADER_CACHE_DIR) + "/" +
                                           std::filesystem::path(path).filename().string() + ".cache";
                    if (!std::filesystem::exists(cache_path)) {
                        shaders_to_load.insert(path);
                    }
                } else {
                    LOG_WARNING(Render_Vulkan, "Shader file not found: {}", path);
                }
            }
        }
    }

    if (shaders_to_load.empty()) {
        LOG_INFO(Render_Vulkan, "All shaders already cached, no preloading needed");
        return;
    }

    LOG_INFO(Render_Vulkan, "Found {} shaders that need preloading", shaders_to_load.size());

    // Use a thread pool to load shaders in parallel
    const size_t max_threads = std::min(std::thread::hardware_concurrency(),
                                      static_cast<unsigned>(4));
    std::vector<std::future<void>> futures;

    for (const auto& path : shaders_to_load) {
        if (!std::filesystem::exists(path)) {
            LOG_WARNING(Render_Vulkan, "Skipping non-existent shader: {}", path);
            continue;
        }

        auto future = std::async(std::launch::async, [this, path]() {
            try {
                this->LoadShader(path);
            } catch (const std::exception& e) {
                LOG_ERROR(Render_Vulkan, "Error loading shader {}: {}", path, e.what());
            }
        });
        futures.push_back(std::move(future));

        // Limit max parallel threads
        if (futures.size() >= max_threads) {
            futures.front().wait();
            futures.erase(futures.begin());
        }
    }

    // Wait for remaining shaders to load
    for (auto& future : futures) {
        future.wait();
    }

    LOG_INFO(Render_Vulkan, "Finished preloading shaders");
}

// Batch load multiple shaders with priorities
void ShaderManager::BatchLoadShaders(const std::vector<std::string>& shader_paths,
                                   const std::vector<u32>& priorities) {
    if (shader_paths.empty()) {
        return;
    }

    LOG_INFO(Render_Vulkan, "Batch loading {} shaders", shader_paths.size());

    for (size_t i = 0; i < shader_paths.size(); ++i) {
        const auto& path = shader_paths[i];
        u32 priority = i < priorities.size() ? priorities[i] : SHADER_PRIORITY_NORMAL;

        AsyncCompileShader(device, path, [this, path](VkShaderModule raw_module) {
            if (raw_module != VK_NULL_HANDLE) {
                // Note: We don't use the raw_module directly as we can't create a proper vk::ShaderModule wrapper.
                // Instead, we'll load the shader again using the LoadShader method which properly handles
                // the creation of the vk::ShaderModule.

                // LoadShader will create the shader module and store it in shader_cache
                if (LoadShader(path)) {
                    LOG_INFO(Render_Vulkan, "Loaded shader module for {}", path);
                } else {
                    LOG_ERROR(Render_Vulkan, "Failed to load shader module for {}", path);
                }
            }
        }, priority);
    }
}

// Preload all shaders in a directory with automatic prioritization
void ShaderManager::PreloadShaderDirectory(const std::string& directory_path) {
    if (!std::filesystem::exists(directory_path)) {
        LOG_WARNING(Render_Vulkan, "Shader directory does not exist: {}", directory_path);
        return;
    }

    std::vector<std::string> shader_paths;
    std::vector<u32> priorities;

    for (const auto& entry : std::filesystem::directory_iterator(directory_path)) {
        if (entry.is_regular_file()) {
            const auto& path = entry.path().string();
            const auto extension = entry.path().extension().string();

            // Only load shader files
            if (extension == ".spv" || extension == ".glsl" || extension == ".vert" ||
                extension == ".frag" || extension == ".comp") {

                shader_paths.push_back(path);

                // Assign priorities based on filename patterns
                // This is a simple heuristic and will be improved
                const auto filename = entry.path().filename().string();
                if (filename.find("ui") != std::string::npos ||
                    filename.find("menu") != std::string::npos) {
                    priorities.push_back(SHADER_PRIORITY_CRITICAL);
                } else if (filename.find("effect") != std::string::npos ||
                         filename.find("post") != std::string::npos) {
                    priorities.push_back(SHADER_PRIORITY_HIGH);
                } else {
                    priorities.push_back(SHADER_PRIORITY_NORMAL);
                }
            }
        }
    }

    if (!shader_paths.empty()) {
        BatchLoadShaders(shader_paths, priorities);
    }
}

// Get current compilation progress
float ShaderManager::GetCompilationProgress() const {
    return GetShaderCompilationProgress();
}

} // namespace Vulkan
