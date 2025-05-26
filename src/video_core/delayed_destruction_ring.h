// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-FileCopyrightText: 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <utility>
#include <vector>
#include <mutex>
#include "common/logging/log.h"

namespace VideoCommon {

/// Container to push objects to be destroyed a few ticks in the future
template <typename T, size_t TICKS_TO_DESTROY>
class DelayedDestructionRing {
public:
    DelayedDestructionRing() = default;
    ~DelayXedDestructionRing() {
        // Ensure all resources are properly released when ring is destroyed
        for (auto& element_list : elements) {
            element_list.clear();
        }
    }

    /// Returns the current index in the ring buffer.
    void Tick() {
        // Increment the index to point to the next slot in the ring buffer
        std::scoped_lock lock{ring_mutex};
        // Increment the index and wrap it around if it exceeds the size of the ring buffer
        index = (index + 1) % TICKS_TO_DESTROY;
        // Clear the current slot in the ring buffer
        const size_t count = elements[index].size();
        if (count > 0) {
            // If more than a threshold of elements are being destroyed at once, log it
            if (count > 100) {
                LOG_DEBUG(Render_Vulkan, "Destroying {} delayed objects", count);
            }
            elements[index].clear();
        }

    }

    void Push(T&& object) {
        std::scoped_lock lock{ring_mutex}; // Ensure thread safety
        elements[index].push_back(std::move(object));
    }

    // Force push an object to be destroyed in the next tick
    void ForceDestroyAll() {
        std::scoped_lock lock{ring_mutex};
        for (auto& element_list : elements) {
            element_list.clear();
        }
        LOG_INFO(Render_Vulkan, "Force destroyed all delayed objects");
    }

    // Returns the number of objects pending destruction
    size_t GetPendingCount() const {
        std::scoped_lock lock{ring_mutex};
        size_t count = 0;
        for (const auto& element_list : elements) {
            count += element_list.size();
        }
        return count;
    }


private:
    size_t index = 0;
    std::array<std::vector<T>, TICKS_TO_DESTROY> elements;
    mutable std::mutex ring_mutex;
};

} // namespace VideoCommon
