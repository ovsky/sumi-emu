// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <memory>
#include <ratio>

#include "common/common_types.h"
#include "core/hardware_properties.h"

namespace Common {

class WallClock {
public:
    static constexpr u64 CNTFRQ = 19'200'000;         // CNTPCT_EL0 Frequency = 19.2 MHz
    static constexpr u64 GPUTickFreq = 614'400'000;   // GM20B GPU Tick Frequency = 614.4 MHz
    // Changed from constexpr to function to get dynamic value from settings
    static inline u64 CPUTickFreq() {
        return Core::Hardware::BASE_CLOCK_RATE();
    } // T210/4 A57 CPU Tick Frequency from settings

    virtual ~WallClock() = default;

    /// @returns The time in nanoseconds since the construction of this clock.
    virtual std::chrono::nanoseconds GetTimeNS() const = 0;

    /// @returns The time in microseconds since the construction of this clock.
    virtual std::chrono::microseconds GetTimeUS() const = 0;

    /// @returns The time in milliseconds since the construction of this clock.
    virtual std::chrono::milliseconds GetTimeMS() const = 0;

    /// @returns The guest CNTPCT ticks since the construction of this clock.
    virtual s64 GetCNTPCT() const = 0;

    /// @returns The guest GPU ticks since the construction of this clock.
    virtual s64 GetGPUTick() const = 0;

    /// @returns The raw host timer ticks since an indeterminate epoch.
    virtual s64 GetUptime() const = 0;

    /// @returns Whether the clock directly uses the host's hardware clock.
    virtual bool IsNative() const = 0;

    static inline u64 NSToCNTPCT(u64 ns) {
        return ns * NsToCNTPCTRatio::num / NsToCNTPCTRatio::den;
    }

    static inline u64 NSToGPUTick(u64 ns) {
        return ns * NsToGPUTickRatio::num / NsToGPUTickRatio::den;
    }

    // Cycle Timing

    static inline u64 CPUTickToNS(u64 cpu_tick) {
        return cpu_tick * CPUTickToNsRatio::num / CPUTickToNsRatio::den;
    }

    static inline u64 CPUTickToUS(u64 cpu_tick) {
        return cpu_tick * CPUTickToUsRatio::num / CPUTickToUsRatio::den;
    }

    static inline u64 CPUTickToCNTPCT(u64 cpu_tick) {
        return cpu_tick * CPUTickToCNTPCTRatio::num / CPUTickToCNTPCTRatio::den;
    }

    static inline u64 CPUTickToGPUTick(u64 cpu_tick) {
        return cpu_tick * CPUTickToGPUTickRatio::num / CPUTickToGPUTickRatio::den;
    }

protected:
    using NsRatio = std::nano;
    using UsRatio = std::micro;
    using MsRatio = std::milli;

    using NsToUsRatio = std::ratio_divide<std::nano, std::micro>;
    using NsToMsRatio = std::ratio_divide<std::nano, std::milli>;
    using NsToCNTPCTRatio = std::ratio<CNTFRQ, std::nano::den>;
    using NsToGPUTickRatio = std::ratio<GPUTickFreq, std::nano::den>;

    // Cycle Timing - using functions for dynamic values

    // Update these to use functions instead of constexpr
    struct CPUTickToNsRatio {
        static inline std::intmax_t num = std::nano::den;
        static inline std::intmax_t den = CPUTickFreq();
    };

    struct CPUTickToUsRatio {
        static inline std::intmax_t num = std::micro::den;
        static inline std::intmax_t den = CPUTickFreq();
    };

    struct CPUTickToCNTPCTRatio {
        static inline std::intmax_t num = CNTFRQ;
        static inline std::intmax_t den = CPUTickFreq();
    };

    struct CPUTickToGPUTickRatio {
        static inline std::intmax_t num = GPUTickFreq;
        static inline std::intmax_t den = CPUTickFreq();
    };
};

std::unique_ptr<WallClock> CreateOptimalClock();

std::unique_ptr<WallClock> CreateStandardWallClock();

} // namespace Common
