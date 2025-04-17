// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <fstream>
#include <algorithm>

#include "common/logging/log.h"
#include "video_core/vulkan_common/hybrid_memory.h"

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#include <sys/syscall.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace Vulkan {

void PredictiveReuseManager::RecordUsage(u64 address, u64 size, bool write_access) {
    std::lock_guard<std::mutex> guard(mutex);

    // Add to history, removing oldest entries if we're past max_history
    access_history.push_back({address, size, write_access, current_timestamp++});
    if (access_history.size() > max_history) {
        access_history.erase(access_history.begin());
    }
}

bool PredictiveReuseManager::IsHotRegion(u64 address, u64 size) const {
    std::lock_guard<std::mutex> guard(mutex);

    // Check if this memory region has been accessed frequently
    const u64 end_address = address + size;
    int access_count = 0;

    for (const auto& access : access_history) {
        const u64 access_end = access.address + access.size;

        // Check for overlap
        if (!(end_address <= access.address || address >= access_end)) {
            access_count++;
        }
    }

    // Consider a region "hot" if it has been accessed in at least 10% of recent accesses
    return access_count >= static_cast<int>(std::max<size_t>(1, max_history / 10));
}

void PredictiveReuseManager::EvictRegion(u64 address, u64 size) {
    std::lock_guard<std::mutex> guard(mutex);

    // Remove any history entries that overlap with this region
    const u64 end_address = address + size;

    access_history.erase(
        std::remove_if(access_history.begin(), access_history.end(),
            [address, end_address](const MemoryAccess& access) {
                const u64 access_end = access.address + access.size;
                // Check for overlap
                return !(end_address <= access.address || address >= access_end);
            }),
        access_history.end()
    );
}

void PredictiveReuseManager::ClearHistory() {
    std::lock_guard<std::mutex> guard(mutex);
    access_history.clear();
    current_timestamp = 0;
}

#if defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)
void FaultManagedAllocator::Touch(size_t addr) {
    lru.remove(addr);
    lru.push_front(addr);
    dirty_set.insert(addr);
}

void FaultManagedAllocator::EnforceLimit() {
    while (lru.size() > MaxPages) {
        size_t evict = lru.back();
        lru.pop_back();

        auto it = page_map.find(evict);
        if (it != page_map.end()) {
            if (dirty_set.count(evict)) {
                // Compress and store dirty page before evicting
                std::vector<u8> compressed((u8*)it->second, (u8*)it->second + PageSize);
                compressed_store[evict] = std::move(compressed);
                dirty_set.erase(evict);
            }

#if defined(__linux__) || defined(__ANDROID__)
            munmap(it->second, PageSize);
#elif defined(_WIN32)
            VirtualFree(it->second, 0, MEM_RELEASE);
#endif
            page_map.erase(it);
        }
    }
}

void* FaultManagedAllocator::GetOrAlloc(size_t addr) {
    std::lock_guard<std::mutex> guard(lock);

    if (page_map.count(addr)) {
        Touch(addr);
        return page_map[addr];
    }

#if defined(__linux__) || defined(__ANDROID__)
    void* mem = mmap(nullptr, PageSize, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (mem == MAP_FAILED) {
        LOG_ERROR(Render_Vulkan, "Failed to mmap memory for fault handler");
        return nullptr;
    }
#elif defined(_WIN32)
    void* mem = VirtualAlloc(nullptr, PageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) {
        LOG_ERROR(Render_Vulkan, "Failed to VirtualAlloc memory for fault handler");
        return nullptr;
    }
#endif

    if (compressed_store.count(addr)) {
        // Decompress stored page data
        std::memcpy(mem, compressed_store[addr].data(), compressed_store[addr].size());
        compressed_store.erase(addr);
    } else {
        std::memset(mem, 0, PageSize);
    }

    page_map[addr] = mem;
    lru.push_front(addr);
    dirty_set.insert(addr);
    EnforceLimit();

    return mem;
}

#if defined(_WIN32)
// Static member initialization
FaultManagedAllocator* FaultManagedAllocator::current_instance = nullptr;

LONG WINAPI FaultManagedAllocator::VectoredExceptionHandler(PEXCEPTION_POINTERS exception_info) {
    // Only handle access violations (page faults)
    if (exception_info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (!current_instance) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Get the faulting address - use ULONG_PTR for Windows
    const ULONG_PTR fault_addr = static_cast<ULONG_PTR>(exception_info->ExceptionRecord->ExceptionInformation[1]);
    const ULONG_PTR base_addr = reinterpret_cast<ULONG_PTR>(current_instance->base_address);

    // Check if the address is within our managed range
    if (fault_addr < base_addr ||
        fault_addr >= (base_addr + static_cast<ULONG_PTR>(current_instance->memory_size))) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Calculate the base address of the page
    const ULONG_PTR page_addr = fault_addr & ~(static_cast<ULONG_PTR>(PageSize) - 1);
    const size_t relative_addr = static_cast<size_t>(page_addr - base_addr);

    // Handle the fault by allocating memory
    void* page = current_instance->GetOrAlloc(relative_addr);
    if (!page) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Copy the page data to the faulting address
    DWORD old_protect;
    void* target_addr = reinterpret_cast<void*>(page_addr);

    // Make the target page writable
    if (VirtualProtect(target_addr, PageSize, PAGE_READWRITE, &old_protect)) {
        std::memcpy(target_addr, page, PageSize);
        // Restore original protection
        VirtualProtect(target_addr, PageSize, old_protect, &old_protect);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

void FaultManagedAllocator::ExceptionHandlerThread() {
    while (running) {
        // Sleep to avoid busy waiting
        Sleep(10);
    }
}
#endif

void FaultManagedAllocator::Initialize(void* base, size_t size) {
#if defined(__linux__) || defined(__ANDROID__)
    uffd = static_cast<int>(syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK));
    if (uffd < 0) {
        LOG_ERROR(Render_Vulkan, "Failed to create userfaultfd, fault handling disabled");
        return;
    }

    struct uffdio_api api = { .api = UFFD_API };
    ioctl(uffd, UFFDIO_API, &api);

    struct uffdio_register reg = {
        .range = { .start = (uintptr_t)base, .len = size },
        .mode = UFFDIO_REGISTER_MODE_MISSING
    };

    if (ioctl(uffd, UFFDIO_REGISTER, &reg) < 0) {
        LOG_ERROR(Render_Vulkan, "Failed to register memory range with userfaultfd");
        close(uffd);
        uffd = -1;
        return;
    }

    running = true;
    fault_handler = std::thread(&FaultManagedAllocator::FaultThread, this);
#elif defined(_WIN32)
    // Setup Windows memory for fault handling
    base_address = base;
    memory_size = size;

    // Reserve memory range but don't commit it yet - it will be demand-paged
    DWORD oldProtect;
    VirtualProtect(base, size, PAGE_NOACCESS, &oldProtect);

    // Install a vectored exception handler
    current_instance = this;
    AddVectoredExceptionHandler(1, VectoredExceptionHandler);

    running = true;
    exception_handler = std::thread(&FaultManagedAllocator::ExceptionHandlerThread, this);

    LOG_INFO(Render_Vulkan, "Windows fault-managed memory initialized at {:p}, size: {}",
             base, size);
#endif
}

#if defined(__linux__) || defined(__ANDROID__)
void FaultManagedAllocator::FaultThread() {
    struct pollfd pfd = { uffd, POLLIN, 0 };

    while (running) {
        if (poll(&pfd, 1, 10) > 0) {
            struct uffd_msg msg;
            read(uffd, &msg, sizeof(msg));

            if (msg.event == UFFD_EVENT_PAGEFAULT) {
                size_t addr = msg.arg.pagefault.address & ~(PageSize - 1);
                void* page = GetOrAlloc(addr);

                if (page) {
                    struct uffdio_copy copy = {
                        .dst = (uintptr_t)addr,
                        .src = (uintptr_t)page,
                        .len = PageSize,
                        .mode = 0
                    };

                    ioctl(uffd, UFFDIO_COPY, &copy);
                }
            }
        }
    }
}
#endif

void* FaultManagedAllocator::Translate(size_t addr) {
    std::lock_guard<std::mutex> guard(lock);

    size_t base = addr & ~(PageSize - 1);
    if (!page_map.count(base)) {
        return nullptr;
    }

    Touch(base);
    return (u8*)page_map[base] + (addr % PageSize);
}

void FaultManagedAllocator::SaveSnapshot(const std::string& path) {
    std::lock_guard<std::mutex> guard(lock);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        LOG_ERROR(Render_Vulkan, "Failed to open snapshot file for writing: {}", path);
        return;
    }

    for (auto& [addr, mem] : page_map) {
        out.write(reinterpret_cast<const char*>(&addr), sizeof(addr));
        out.write(reinterpret_cast<const char*>(mem), PageSize);
    }

    LOG_INFO(Render_Vulkan, "Saved memory snapshot to {}", path);
}

void FaultManagedAllocator::SaveDifferentialSnapshot(const std::string& path) {
    std::lock_guard<std::mutex> guard(lock);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        LOG_ERROR(Render_Vulkan, "Failed to open diff snapshot file for writing: {}", path);
        return;
    }

    size_t dirty_count = 0;
    for (const auto& addr : dirty_set) {
        if (page_map.count(addr)) {
            out.write(reinterpret_cast<const char*>(&addr), sizeof(addr));
            out.write(reinterpret_cast<const char*>(page_map[addr]), PageSize);
            dirty_count++;
        }
    }

    LOG_INFO(Render_Vulkan, "Saved differential snapshot to {} ({} dirty pages)",
             path, dirty_count);
}

void FaultManagedAllocator::ClearDirtySet() {
    std::lock_guard<std::mutex> guard(lock);
    dirty_set.clear();
    LOG_DEBUG(Render_Vulkan, "Cleared dirty page tracking");
}

FaultManagedAllocator::~FaultManagedAllocator() {
    running = false;

#if defined(__linux__) || defined(__ANDROID__)
    if (fault_handler.joinable()) {
        fault_handler.join();
    }

    for (auto& [addr, mem] : page_map) {
        munmap(mem, PageSize);
    }

    if (uffd != -1) {
        close(uffd);
    }
#elif defined(_WIN32)
    if (exception_handler.joinable()) {
        exception_handler.join();
    }

    // Remove the vectored exception handler
    RemoveVectoredExceptionHandler(VectoredExceptionHandler);
    current_instance = nullptr;

    for (auto& [addr, mem] : page_map) {
        VirtualFree(mem, 0, MEM_RELEASE);
    }

    // Free the base memory if needed
    if (base_address) {
        VirtualFree(base_address, 0, MEM_RELEASE);
        base_address = nullptr;
    }
#endif
}
#endif // defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)

HybridMemory::HybridMemory(const Device& device_, MemoryAllocator& allocator, size_t reuse_history)
    : device(device_), memory_allocator(allocator), reuse_manager(reuse_history) {
}

HybridMemory::~HybridMemory() = default;

void HybridMemory::InitializeGuestMemory(void* base, size_t size) {
#if defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)
    fmaa.Initialize(base, size);
    LOG_INFO(Render_Vulkan, "Initialized fault-managed guest memory at {:p}, size: {}",
             base, size);
#else
    LOG_INFO(Render_Vulkan, "Fault-managed memory not supported on this platform");
#endif
}

void* HybridMemory::TranslateAddress(size_t addr) {
#if defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)
    return fmaa.Translate(addr);
#else
    return nullptr;
#endif
}

ComputeBuffer HybridMemory::CreateComputeBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                               MemoryUsage memory_type) {
    ComputeBuffer buffer;
    buffer.size = size;

    VkBufferCreateInfo buffer_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    // Using CreateBuffer directly handles memory allocation internally
    buffer.buffer = memory_allocator.CreateBuffer(buffer_ci, memory_type);

    LOG_DEBUG(Render_Vulkan, "Created compute buffer: size={}, usage={:x}",
              size, usage);

    return buffer;
}

void HybridMemory::SaveSnapshot(const std::string& path) {
#if defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)
    fmaa.SaveSnapshot(path);
#else
    LOG_ERROR(Render_Vulkan, "Memory snapshots not supported on this platform");
#endif
}

void HybridMemory::SaveDifferentialSnapshot(const std::string& path) {
#if defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)
    fmaa.SaveDifferentialSnapshot(path);
#else
    LOG_ERROR(Render_Vulkan, "Differential memory snapshots not supported on this platform");
#endif
}

void HybridMemory::ResetDirtyTracking() {
#if defined(__linux__) || defined(__ANDROID__) || defined(_WIN32)
    fmaa.ClearDirtySet();
#endif
}

} // namespace Vulkan