// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"
#include "core/hle/service/kernel_helpers.h"

namespace Core {
class System;
}

namespace Service::Sockets {

class BSDCFG final : public ServiceFramework<BSDCFG> {
public:
    explicit BSDCFG(Core::System& system_);
    ~BSDCFG() override;

private:
    void SetIfUp(HLERequestContext& ctx);
    void SetIfUpWithEvent(HLERequestContext& ctx);
    void CancelIf(HLERequestContext& ctx);
    void SetIfDown(HLERequestContext& ctx);
    void GetIfState(HLERequestContext& ctx);
    void DhcpRenew(HLERequestContext& ctx);
    void AddStaticArpEntry(HLERequestContext& ctx);
    void RemoveArpEntry(HLERequestContext& ctx);
    void LookupArpEntry(HLERequestContext& ctx);
    void LookupArpEntry2(HLERequestContext& ctx);
    void ClearArpEntries(HLERequestContext& ctx);
    void ClearArpEntries2(HLERequestContext& ctx);
    void PrintArpEntries(HLERequestContext& ctx);
    void Cmd13(HLERequestContext& ctx);
    void Cmd14(HLERequestContext& ctx);
    void Cmd15(HLERequestContext& ctx);

    KernelHelpers::ServiceContext service_context;
};

} // namespace Service::Sockets