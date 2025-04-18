// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include <list>
#include <set>
#include <map>
#include <thread>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

struct ComputeBuffer {
    vk::Buffer buffer{};
    VkDeviceSize size = 0;
};

class PredictiveReuseManager {
public:
    explicit PredictiveReuseManager(size_t history_size) : max_history{history_size} {}

    void RecordUsage(u64 address, u64 size, bool write_access);
    bool IsHotRegion(u64 address, u64 size) const;
    void EvictRegion(u64 address, u64 size);
    void ClearHistory();

private:
    struct MemoryAccess {
        u64 address;
        u64 size;
        bool write_access;
        u64 timestamp;
    };

    std::vector<MemoryAccess> access_history;
    const size_t max_history;
    u64 current_timestamp{0};
    mutable std::mutex mutex;
};

#if defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)
class FaultManagedAllocator {
public:
    static constexpr size_t PageSize = 0x1000;
    static constexpr size_t MaxPages = 16384;

    void Initialize(void* base, size_t size);
    void* Translate(size_t addr);
    void SaveSnapshot(const std::string& path);
    void SaveDifferentialSnapshot(const std::string& path);
    void ClearDirtySet();
    ~FaultManagedAllocator();

private:
    std::map<size_t, void*> page_map;
    std::list<size_t> lru;
    std::set<size_t> dirty_set;
    std::unordered_map<size_t, std::vector<u8>> compressed_store;
    std::mutex lock;

#if defined(__linux__) || defined(__ANDROID__)
    int uffd = -1;
    std::atomic<bool> running{false};
    std::thread fault_handler;
    void FaultThread();
#elif defined(_WIN32)
    void* base_address = nullptr;
    size_t memory_size = 0;
    HANDLE exception_port = nullptr;
    std::atomic<bool> running{false};
    std::thread exception_handler;
    void ExceptionHandlerThread();
    static LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS exception_info);
    static FaultManagedAllocator* current_instance;
#endif

    void Touch(size_t addr);
    void EnforceLimit();
    void* GetOrAlloc(size_t addr);
};
#endif

class HybridMemory {
public:
    explicit HybridMemory(const Device& device, MemoryAllocator& allocator, size_t reuse_history = 32);
    ~HybridMemory();

    void InitializeGuestMemory(void* base, size_t size);
    void* TranslateAddress(size_t addr);

    ComputeBuffer CreateComputeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory_type);

    void SaveSnapshot(const std::string& path);
    void SaveDifferentialSnapshot(const std::string& path);
    void ResetDirtyTracking();

private:
    const Device& device;
    MemoryAllocator& memory_allocator;
    PredictiveReuseManager reuse_manager;

#if defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)
    FaultManagedAllocator fmaa;
#endif
};

} // namespace Vulkan