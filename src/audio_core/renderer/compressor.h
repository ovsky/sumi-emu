// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include "common/common_types.h"

namespace AudioCore::Renderer {

struct CompressorStatistics {
    float maximum_mean{};
    float minimum_gain{1.0f};
    std::array<float, 6> last_samples{}; // 6 channels max

    void Reset(u16 channel_count) {
        maximum_mean = 0.0f;
        minimum_gain = 1.0f;
        std::fill_n(last_samples.begin(), channel_count, 0.0f);
    }
};

struct CompressorParameter {
    u32 channel_count{};
    float input_gain{};
    float release_coefficient{};
    float attack_coefficient{};
    float ratio{};
    float threshold{};
    bool makeup_gain_enabled{};
    bool statistics_enabled{};
    bool statistics_reset{};

    bool IsChannelCountValid() const {
        return channel_count > 0 && channel_count <= 6;
    }
};

} // namespace AudioCore::Renderer
