// SPDX-FileCopyrightText: Ryujinx Team and Contributors
// SPDX-License-Identifier: MIT

#include <bit>

#include "common/thread.h"
#include "core/core.h"
#include "video_core/cdma_pusher.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/host1x/control.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/nvdec.h"
#include "video_core/host1x/nvdec_common.h"
#include "video_core/host1x/vic.h"
#include "video_core/memory_manager.h"

namespace Tegra {

CDmaPusher::CDmaPusher(Host1x::Host1x& host1x_, s32 id)
    : host1x{host1x_}, memory_manager{host1x.GMMU()},
      host_processor{std::make_unique<Host1x::Control>(host1x_)}, current_class{
                                                                      static_cast<ChClassId>(id)} {
    thread = std::jthread([this](std::stop_token stop_token) { ProcessEntries(stop_token); });
}

CDmaPusher::~CDmaPusher() = default;

void CDmaPusher::ProcessEntries(std::stop_token stop_token) {
    Common::SetCurrentThreadPriority(Common::ThreadPriority::High);
    ChCommandHeaderList command_list{host1x.System().ApplicationMemory(), 0, 0};
    u32 count{};
    u32 method_offset{};
    u32 mask{};
    bool incrementing{};

    while (!stop_token.stop_requested()) {
        {
            std::unique_lock l{command_mutex};
            Common::CondvarWait(command_cv, l, stop_token,
                                [this]() { return command_lists.size() > 0; });
            if (stop_token.stop_requested()) {
                return;
            }

            command_list = std::move(command_lists.front());
            command_lists.pop_front();
        }

        size_t i = 0;
        for (const auto value : command_list) {
            i++;
            if (mask != 0) {
                const auto lbs = static_cast<u32>(std::countr_zero(mask));
                mask &= ~(1U << lbs);
                ExecuteCommand(method_offset + lbs, value.raw);
                continue;
            } else if (count != 0) {
                --count;
                ExecuteCommand(method_offset, value.raw);
                if (incrementing) {
                    ++method_offset;
                }
                continue;
            }
            const auto mode = value.submission_mode.Value();
            switch (mode) {
            case ChSubmissionMode::SetClass: {
                mask = value.value & 0x3f;
                method_offset = value.method_offset;
                current_class = static_cast<ChClassId>((value.value >> 6) & 0x3ff);
                break;
            }
            case ChSubmissionMode::Incrementing:
            case ChSubmissionMode::NonIncrementing:
                count = value.value;
                method_offset = value.method_offset;
                incrementing = mode == ChSubmissionMode::Incrementing;
                break;
            case ChSubmissionMode::Mask:
                mask = value.value;
                method_offset = value.method_offset;
                break;
            case ChSubmissionMode::Immediate: {
                const u32 data = value.value & 0xfff;
                method_offset = value.method_offset;
                ExecuteCommand(method_offset, data);
                break;
            }
            default:
                LOG_ERROR(HW_GPU, "Bad command at index {} (bytes 0x{:X}), buffer size {}", i - 1,
                          (i - 1) * sizeof(u32), command_list.size());
                UNIMPLEMENTED_MSG("ChSubmission mode {} is not implemented!",
                                  static_cast<u32>(mode));
                break;
            }
        }
    }
}

void CDmaPusher::ExecuteCommand(u32 method, u32 arg) {
    switch (current_class) {
    case ChClassId::Control:
        LOG_TRACE(Service_NVDRV, "Class {} method 0x{:X} arg 0x{:X}",
                  static_cast<u32>(current_class), method, arg);
        host_processor->ProcessMethod(static_cast<Host1x::Control::Method>(method), arg);
        break;
    default:
        thi_regs.reg_array[method] = arg;
        switch (static_cast<ThiMethod>(method)) {
        case ThiMethod::IncSyncpt: {
            const auto syncpoint_id = static_cast<u32>(arg & 0xFF);
            [[maybe_unused]] const auto cond = static_cast<u32>((arg >> 8) & 0xFF);
            LOG_TRACE(Service_NVDRV, "Class {} IncSyncpt Method, syncpt {} cond {}",
                      static_cast<u32>(current_class), syncpoint_id, cond);
            auto& syncpoint_manager = host1x.GetSyncpointManager();
            syncpoint_manager.IncrementGuest(syncpoint_id);
            syncpoint_manager.IncrementHost(syncpoint_id);
            break;
        }
        case ThiMethod::SetMethod1:
            LOG_TRACE(Service_NVDRV, "Class {} method 0x{:X} arg 0x{:X}",
                      static_cast<u32>(current_class), static_cast<u32>(thi_regs.method_0), arg);
            ProcessMethod(thi_regs.method_0, arg);
            break;
        default:
            break;
        }
    }
}

} // namespace Tegra
