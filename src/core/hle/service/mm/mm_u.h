// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2018 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

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
    TEST = 8
};

typedef u32 Priority;
typedef u32 Setting;

void LoopProcess(Core::System& system);

} // namespace Service::MM