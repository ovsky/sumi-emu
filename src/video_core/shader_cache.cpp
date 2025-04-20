// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "common/assert.h"
#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"
#include "common/thread_worker.h"
#include "shader_recompiler/frontend/maxwell/control_flow.h"
#include "shader_recompiler/object_pool.h"
#include "video_core/control/channel_state.h"
#include "video_core/dirty_flags.h"
#include "video_core/engines/kepler_compute.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/memory_manager.h"
#include "video_core/shader_cache.h"
#include "video_core/shader_environment.h"

namespace VideoCommon {

constexpr size_t MAX_SHADER_CACHE_SIZE = 1024 * 1024 * 1024; // 1GB

class ShaderCacheWorker : public Common::ThreadWorker {
public:
    explicit ShaderCacheWorker(const std::string& name) : ThreadWorker(name) {}
    ~ShaderCacheWorker() = default;

    void CompileShader(ShaderInfo* shader) {
        Push([shader]() {
            // Compile shader here
            // This is a placeholder for the actual compilation process
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            shader->is_compiled.store(true, std::memory_order_release);
        });
    }
};

class ShaderCache::Impl {
public:
    explicit Impl(Tegra::MaxwellDeviceMemoryManager& device_memory_)
        : device_memory{device_memory_}, workers{CreateWorkers()} {
        LoadCache();
    }

    ~Impl() {
        SaveCache();
    }

    void InvalidateRegion(VAddr addr, size_t size) {
        std::scoped_lock lock{invalidation_mutex};
        InvalidatePagesInRegion(addr, size);
        RemovePendingShaders();
    }

    void OnCacheInvalidation(VAddr addr, size_t size) {
        std::scoped_lock lock{invalidation_mutex};
        InvalidatePagesInRegion(addr, size);
    }

    void SyncGuestHost() {
        std::scoped_lock lock{invalidation_mutex};
        RemovePendingShaders();
    }

    bool RefreshStages(std::array<u64, 6>& unique_hashes);
    const ShaderInfo* ComputeShader();
    void GetGraphicsEnvironments(GraphicsEnvironments& result, const std::array<u64, NUM_PROGRAMS>& unique_hashes);

    ShaderInfo* TryGet(VAddr addr) const {
        std::scoped_lock lock{lookup_mutex};

        const auto it = lookup_cache.find(addr);
        if (it == lookup_cache.end()) {
            return nullptr;
        }
        return it->second->data;
    }

    void Register(std::unique_ptr<ShaderInfo> data, VAddr addr, size_t size) {
        std::scoped_lock lock{invalidation_mutex, lookup_mutex};

        const VAddr addr_end = addr + size;
        Entry* const entry = NewEntry(addr, addr_end, data.get());

        const u64 page_end = (addr_end + SUMI_PAGESIZE - 1) >> SUMI_PAGEBITS;
        for (u64 page = addr >> SUMI_PAGEBITS; page < page_end; ++page) {
            invalidation_cache[page].push_back(entry);
        }

        storage.push_back(std::move(data));

        device_memory.UpdatePagesCachedCount(addr, size, 1);
    }

private:
    std::vector<std::unique_ptr<ShaderCacheWorker>> CreateWorkers() {
        const size_t num_workers = std::thread::hardware_concurrency();
        std::vector<std::unique_ptr<ShaderCacheWorker>> workers;
        workers.reserve(num_workers);
        for (size_t i = 0; i < num_workers; ++i) {
            workers.emplace_back(std::make_unique<ShaderCacheWorker>(fmt::format("ShaderWorker{}", i)));
        }
        return workers;
    }

    void LoadCache() {
        const auto cache_dir = Common::FS::GetSumiPath(Common::FS::SumiPath::ShaderDir);
        std::filesystem::create_directories(cache_dir);

        const auto cache_file = cache_dir / "shader_cache.bin";
        if (!std::filesystem::exists(cache_file)) {
            return;
        }

        std::ifstream file(cache_file, std::ios::binary);
        if (!file) {
            LOG_ERROR(Render_Vulkan, "Failed to open shader cache file for reading");
            return;
        }

        size_t num_entries;
        file.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));

        for (size_t i = 0; i < num_entries; ++i) {
            VAddr addr;
            size_t size;
            file.read(reinterpret_cast<char*>(&addr), sizeof(addr));
            file.read(reinterpret_cast<char*>(&size), sizeof(size));

            auto info = std::make_unique<ShaderInfo>();
            file.read(reinterpret_cast<char*>(info.get()), sizeof(ShaderInfo));

            Register(std::move(info), addr, size);
        }
    }

    void SaveCache() {
        const auto cache_dir = Common::FS::GetSumiPath(Common::FS::SumiPath::ShaderDir);
        std::filesystem::create_directories(cache_dir);

        const auto cache_file = cache_dir / "shader_cache.bin";
        std::ofstream file(cache_file, std::ios::binary | std::ios::trunc);
        if (!file) {
            LOG_ERROR(Render_Vulkan, "Failed to open shader cache file for writing");
            return;
        }

        const size_t num_entries = storage.size();
        file.write(reinterpret_cast<const char*>(&num_entries), sizeof(num_entries));

        for (const auto& shader : storage) {
            const VAddr addr = shader->addr;
            const size_t size = shader->size_bytes;
            file.write(reinterpret_cast<const char*>(&addr), sizeof(addr));
            file.write(reinterpret_cast<const char*>(&size), sizeof(size));
            file.write(reinterpret_cast<const char*>(shader.get()), sizeof(ShaderInfo));
        }
    }

    void InvalidatePagesInRegion(VAddr addr, size_t size) {
        const VAddr addr_end = addr + size;
        const u64 page_end = (addr_end + SUMI_PAGESIZE - 1) >> SUMI_PAGEBITS;
        for (u64 page = addr >> SUMI_PAGEBITS; page < page_end; ++page) {
            auto it = invalidation_cache.find(page);
            if (it == invalidation_cache.end()) {
                continue;
            }
            InvalidatePageEntries(it->second, addr, addr_end);
        }
    }

    void RemovePendingShaders() {
        if (marked_for_removal.empty()) {
            return;
        }
        // Remove duplicates
        std::sort(marked_for_removal.begin(), marked_for_removal.end());
        marked_for_removal.erase(std::unique(marked_for_removal.begin(), marked_for_removal.end()),
                                 marked_for_removal.end());

        std::vector<ShaderInfo*> removed_shaders;

        std::scoped_lock lock{lookup_mutex};
        for (Entry* const entry : marked_for_removal) {
            removed_shaders.push_back(entry->data);

            const auto it = lookup_cache.find(entry->addr_start);
            ASSERT(it != lookup_cache.end());
            lookup_cache.erase(it);
        }
        marked_for_removal.clear();

        if (!removed_shaders.empty()) {
            RemoveShadersFromStorage(removed_shaders);
        }
    }

    void InvalidatePageEntries(std::vector<Entry*>& entries, VAddr addr, VAddr addr_end) {
        size_t index = 0;
        while (index < entries.size()) {
            Entry* const entry = entries[index];
            if (!entry->Overlaps(addr, addr_end)) {
                ++index;
                continue;
            }

            UnmarkMemory(entry);
            RemoveEntryFromInvalidationCache(entry);
            marked_for_removal.push_back(entry);
        }
    }

    void RemoveEntryFromInvalidationCache(const Entry* entry) {
        const u64 page_end = (entry->addr_end + SUMI_PAGESIZE - 1) >> SUMI_PAGEBITS;
        for (u64 page = entry->addr_start >> SUMI_PAGEBITS; page < page_end; ++page) {
            const auto entries_it = invalidation_cache.find(page);
            ASSERT(entries_it != invalidation_cache.end());
            std::vector<Entry*>& entries = entries_it->second;

            const auto entry_it = std::find(entries.begin(), entries.end(), entry);
            ASSERT(entry_it != entries.end());
            entries.erase(entry_it);
        }
    }

    void UnmarkMemory(Entry* entry) {
        if (!entry->is_memory_marked) {
            return;
        }
        entry->is_memory_marked = false;

        const VAddr addr = entry->addr_start;
        const size_t size = entry->addr_end - addr;
        device_memory.UpdatePagesCachedCount(addr, size, -1);
    }

    void RemoveShadersFromStorage(const std::vector<ShaderInfo*>& removed_shaders) {
        storage.erase(
            std::remove_if(storage.begin(), storage.end(),
                           [&removed_shaders](const std::unique_ptr<ShaderInfo>& shader) {
                               return std::find(removed_shaders.begin(), removed_shaders.end(),
                                                shader.get()) != removed_shaders.end();
                           }),
            storage.end());
    }

    Entry* NewEntry(VAddr addr, VAddr addr_end, ShaderInfo* data) {
        auto entry = std::make_unique<Entry>(Entry{addr, addr_end, data});
        Entry* const entry_pointer = entry.get();

        lookup_cache.emplace(addr, std::move(entry));
        return entry_pointer;
    }

    Tegra::MaxwellDeviceMemoryManager& device_memory;
    std::vector<std::unique_ptr<ShaderCacheWorker>> workers;

    mutable std::mutex lookup_mutex;
    std::mutex invalidation_mutex;

    std::unordered_map<VAddr, std::unique_ptr<Entry>> lookup_cache;
    std::unordered_map<u64, std::vector<Entry*>> invalidation_cache;
    std::vector<std::unique_ptr<ShaderInfo>> storage;
    std::vector<Entry*> marked_for_removal;
};

ShaderCache::ShaderCache(Tegra::MaxwellDeviceMemoryManager& device_memory_)
    : impl{std::make_unique<Impl>(device_memory_)} {}

ShaderCache::~ShaderCache() = default;

void ShaderCache::InvalidateRegion(VAddr addr, size_t size) {
    impl->InvalidateRegion(addr, size);
}

void ShaderCache::OnCacheInvalidation(VAddr addr, size_t size) {
    impl->OnCacheInvalidation(addr, size);
}

void ShaderCache::SyncGuestHost() {
    impl->SyncGuestHost();
}

bool ShaderCache::RefreshStages(std::array<u64, 6>& unique_hashes) {
    return impl->RefreshStages(unique_hashes);
}

const ShaderInfo* ShaderCache::ComputeShader() {
    return impl->ComputeShader();
}

void ShaderCache::GetGraphicsEnvironments(GraphicsEnvironments& result,
                                          const std::array<u64, NUM_PROGRAMS>& unique_hashes) {
    impl->GetGraphicsEnvironments(result, unique_hashes);
}

ShaderInfo* ShaderCache::TryGet(VAddr addr) const {
    return impl->TryGet(addr);
}

void ShaderCache::Register(std::unique_ptr<ShaderInfo> data, VAddr addr, size_t size) {
    impl->Register(std::move(data), addr, size);
}

} // namespace VideoCommon
