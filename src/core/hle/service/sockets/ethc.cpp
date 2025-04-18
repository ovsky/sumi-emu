// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/ethc.h"

namespace Service::Sockets {

ETHC_C::ETHC_C(Core::System& system_) : ServiceFramework{system_, "ethc:c"} {
    // ethc:c doesn't have documented commands yet
    static const FunctionInfo functions[] = {
        {0, nullptr, "DummyFunction"},
    };

    RegisterHandlers(functions);
}

ETHC_C::~ETHC_C() = default;

ETHC_I::ETHC_I(Core::System& system_) : ServiceFramework{system_, "ethc:i"} {
    // ethc:i doesn't have documented commands yet
    static const FunctionInfo functions[] = {
        {0, nullptr, "DummyFunction"},
    };

    RegisterHandlers(functions);
}

ETHC_I::~ETHC_I() = default;

} // namespace Service::Sockets