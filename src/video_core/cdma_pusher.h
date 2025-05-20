// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/polyfill_thread.h"
#include "core/memory.h"

namespace Tegra {

namespace Host1x {
class Control;
class Host1x;
class Nvdec;
class SyncptIncrManager;
class Vic;
} // namespace Host1x

enum class ChSubmissionMode : u32 {
    SetClass = 0,
    Incrementing = 1,
    NonIncrementing = 2,
    Mask = 3,
    Immediate = 4,
    Restart = 5,
    Gather = 6,
};

enum class ChClassId : u32 {
    NoClass = 0x0,
    Control = 0x1,
    VideoEncodeMpeg = 0x20,
    VideoEncodeNvEnc = 0x21,
    VideoStreamingVi = 0x30,
    VideoStreamingIsp = 0x32,
    VideoStreamingIspB = 0x34,
    VideoStreamingViI2c = 0x36,
    GraphicsVic = 0x5d,
    Graphics3D = 0x60,
    GraphicsGpu = 0x61,
    Tsec = 0xe0,
    TsecB = 0xe1,
    NvJpg = 0xc0,
    NvDec = 0xf0
};

union ChCommandHeader {
    u32 raw;
    BitField<0, 16, u32> value;
    BitField<16, 12, u32> method_offset;
    BitField<28, 4, ChSubmissionMode> submission_mode;
};
static_assert(sizeof(ChCommandHeader) == sizeof(u32), "ChCommand header is an invalid size");

struct ChCommand {
    ChClassId class_id{};
    int method_offset{};
    std::vector<u32> arguments;
};

using ChCommandHeaderList =
    Core::Memory::CpuGuestMemory<Tegra::ChCommandHeader, Core::Memory::GuestMemoryFlags::SafeRead>;

struct ThiRegisters {
    static constexpr std::size_t NUM_REGS = 0x20;

    union {
        struct {
            u32_le increment_syncpt;
            INSERT_PADDING_WORDS_NOINIT(1);
            u32_le increment_syncpt_error;
            u32_le ctx_switch_incremement_syncpt;
            INSERT_PADDING_WORDS_NOINIT(4);
            u32_le ctx_switch;
            INSERT_PADDING_WORDS_NOINIT(1);
            u32_le ctx_syncpt_eof;
            INSERT_PADDING_WORDS_NOINIT(5);
            u32_le method_0;
            u32_le method_1;
            INSERT_PADDING_WORDS_NOINIT(12);
            u32_le int_status;
            u32_le int_mask;
        };
        std::array<u32, NUM_REGS> reg_array;
    };
};

enum class ThiMethod : u32 {
    IncSyncpt = offsetof(ThiRegisters, increment_syncpt) / sizeof(u32),
    SetMethod0 = offsetof(ThiRegisters, method_0) / sizeof(u32),
    SetMethod1 = offsetof(ThiRegisters, method_1) / sizeof(u32),
};

class CDmaPusher {
public:
    CDmaPusher() = delete;
    virtual ~CDmaPusher();

    void PushEntries(ChCommandHeaderList&& entries) {
        std::scoped_lock l{command_mutex};
        command_lists.push_back(std::move(entries));
        command_cv.notify_one();
    }

protected:
    explicit CDmaPusher(Host1x::Host1x& host1x, s32 id);

    virtual void ProcessMethod(u32 method, u32 arg) = 0;

    Host1x::Host1x& host1x;
    Tegra::MemoryManager& memory_manager;

private:
    /// Process the command entry
    void ProcessEntries(std::stop_token stop_token);

    /// Invoke command class devices to execute the command based on the current state
    void ExecuteCommand(u32 state_offset, u32 data);

    std::unique_ptr<Host1x::Control> host_processor;

    std::mutex command_mutex;
    std::condition_variable_any command_cv;
    std::deque<ChCommandHeaderList> command_lists;
    std::jthread thread;

    ThiRegisters thi_regs{};
    ChClassId current_class;
};

} // namespace Tegra
