// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 Sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>
#include "common/logging/log.h"

namespace VideoCommon {

/// Container to push objects to be destroyed a few ticks in the future
template <typename T, size_t TICKS_TO_DESTROY>
class DelayedDestructionRing {
public:
    DelayedDestructionRing() = default;
    ~DelayedDestructionRing() {
        // Ensure all resources are properly released when ring is destroyed
        for (auto& element_list : elements) {
            element_list.clear();
        }
    }

    void Tick() {
        std::scoped_lock lock{ring_mutex};

        // Move to next position in the ring
        index = (index + 1) % TICKS_TO_DESTROY;

        // Clear elements at current position, which ensures resources are properly released
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
        std::scoped_lock lock{ring_mutex};
        elements[index].push_back(std::move(object));
    }

    // Force immediate destruction of all resources (for emergency cleanup)
    void ForceDestroyAll() {
        std::scoped_lock lock{ring_mutex};
        for (auto& element_list : elements) {
            element_list.clear();
        }
        LOG_INFO(Render_Vulkan, "Force destroyed all delayed objects");
    }

    // Get current number of pending resources awaiting destruction
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