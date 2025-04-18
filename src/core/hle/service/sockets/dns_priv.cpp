// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/dns_priv.h"

namespace Service::Sockets {

DNS_PRIV::DNS_PRIV(Core::System& system_) : ServiceFramework{system_, "dns:priv"} {
    // dns:priv doesn't have documented commands yet
    static const FunctionInfo functions[] = {
        {0, nullptr, "DummyFunction"},
    };

    RegisterHandlers(functions);
}

DNS_PRIV::~DNS_PRIV() = default;

} // namespace Service::Sockets