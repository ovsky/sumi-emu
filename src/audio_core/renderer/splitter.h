// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace AudioCore::Renderer {

class SplitterDestination {
public:
    void Update(const SplitterDestinationParameter& parameter, bool is_prev_volume_reset_supported) {
        if (is_prev_volume_reset_supported ? parameter.reset_prev_volume
                                         : (!is_used && parameter.is_used)) {
            // Reset previous mix volumes
            prev_mix_volumes = parameter.mix_volumes;
            mix_volumes = parameter.mix_volumes;
        }
        is_used = parameter.is_used;
    }

private:
    bool is_used{};
    std::array<float, MaxMixBuffers> mix_volumes{};
    std::array<float, MaxMixBuffers> prev_mix_volumes{};
};

} // namespace AudioCore::Renderer
