// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cmath>
#include "audio_core/renderer/effect/compressor.h"

namespace AudioCore::Renderer {

void CompressorInfo::Update(BehaviorInfo::ErrorInfo& error_info,
                            const InParameterVersion1& in_params, const PoolMapper& pool_mapper) {}

void CompressorInfo::Update(BehaviorInfo::ErrorInfo& error_info,
                            const InParameterVersion2& in_params, const PoolMapper& pool_mapper) {
    auto in_specific{reinterpret_cast<const ParameterVersion1*>(in_params.specific.data())};
    auto params{reinterpret_cast<ParameterVersion1*>(parameter.data())};

    std::memcpy(params, in_specific, sizeof(ParameterVersion1));
    mix_id = in_params.mix_id;
    process_order = in_params.process_order;
    enabled = in_params.enabled;

    error_info.error_code = ResultSuccess;
    error_info.address = CpuAddr(0);
}

void CompressorInfo::UpdateForCommandGeneration() {
    if (enabled) {
        usage_state = UsageState::Enabled;
    } else {
        usage_state = UsageState::Disabled;
    }

    auto params{reinterpret_cast<ParameterVersion1*>(parameter.data())};
    params->state = ParameterState::Updated;
}

CpuAddr CompressorInfo::GetWorkbuffer(s32 index) {
    return GetSingleBuffer(index);
}

CompressorEffect::CompressorEffect(std::size_t sample_count_) : sample_count{sample_count_} {}

void CompressorEffect::Process(std::span<f32> output_buffer, std::span<const f32> input_buffer) {
    if (!IsEnabled() || !parameter.IsChannelCountValid()) {
        std::copy(input_buffer.begin(), input_buffer.end(), output_buffer.begin());
        return;
    }

    if (!result_state.empty() && parameter.statistics_reset) {
        statistics.Reset(static_cast<u16>(parameter.channel_count));
    }

    // Process audio with compressor effect
    for (u32 sample_index = 0; sample_index < sample_count; sample_index++) {
        float mean = 0.0f;
        for (u32 channel = 0; channel < parameter.channel_count; channel++) {
            const auto sample = input_buffer[sample_index * parameter.channel_count + channel];
            mean += sample * sample;
        }
        mean /= static_cast<float>(parameter.channel_count);

        // Calculate compression gain
        float compression_gain = 1.0f;
        if (mean > parameter.threshold) {
            compression_gain = parameter.threshold / mean;
            compression_gain = std::pow(compression_gain, 1.0f - (1.0f / parameter.ratio));
        }

        // Apply compression
        for (u32 channel = 0; channel < parameter.channel_count; channel++) {
            const auto in_sample = input_buffer[sample_index * parameter.channel_count + channel];
            const auto out_sample = in_sample * compression_gain * parameter.input_gain;
            output_buffer[sample_index * parameter.channel_count + channel] = out_sample;

            // Update statistics if enabled
            if (parameter.statistics_enabled) {
                statistics.maximum_mean = std::max(statistics.maximum_mean, mean);
                statistics.minimum_gain = std::min(statistics.minimum_gain, compression_gain);
                statistics.last_samples[channel] = std::abs(in_sample) * (1.0f / 32768.0f);
            }
        }
    }
}

} // namespace AudioCore::Renderer
