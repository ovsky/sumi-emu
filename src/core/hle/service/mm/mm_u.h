// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>
#include "common/common_types.h"

namespace Core {
class System;
}

namespace Service::MM {

enum class Module : u32 {
    CPU = 0,
    GPU = 1,
    EMC = 2,
    SYS_BUS = 3,
    M_SELECT = 4,
    NVDEC = 5,
    NVENC = 6,
    NVJPG = 7,
    TEST = 8,
};

using Priority = u32;
using Setting = u32;

enum class EventClearMode : u32 {
    Manual = 0,
    Auto = 1,
};

// Consolidate settings into a struct for better organization
struct Settings {
    Setting min{0};
    Setting max{0};
    Setting current{0};
    u32 id{1};  // Used by newer API versions
};

class Session {
public:
    explicit Session(Module module_, u32 request_id_, EventClearMode clear_mode_)
        : module{module_}, request_id{request_id_}, clear_mode{clear_mode_} {}

    void SetAndWait(u32 min_, s32 max_) {
        min = min_;
        max = max_;
    }

    [[nodiscard]] u32 GetMin() const { return min; }
    [[nodiscard]] Module GetModule() const { return module; }
    [[nodiscard]] u32 GetRequestId() const { return request_id; }

private:
    Module module;
    u32 request_id{0};
    u32 min{0};
    s32 max{-1};
    EventClearMode clear_mode;
};

void LoopProcess(Core::System& system);

} // namespace Service::MM
