// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/sockets/bsdcfg.h"

namespace Service::Sockets {

BSDCFG::BSDCFG(Core::System& system_)
    : ServiceFramework{system_, "bsdcfg"},
      service_context{system_, "BSDCFG"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &BSDCFG::SetIfUp, "SetIfUp"},
        {1, &BSDCFG::SetIfUpWithEvent, "SetIfUpWithEvent"},
        {2, &BSDCFG::CancelIf, "CancelIf"},
        {3, &BSDCFG::SetIfDown, "SetIfDown"},
        {4, &BSDCFG::GetIfState, "GetIfState"},
        {5, &BSDCFG::DhcpRenew, "DhcpRenew"},
        {6, &BSDCFG::AddStaticArpEntry, "AddStaticArpEntry"},
        {7, &BSDCFG::RemoveArpEntry, "RemoveArpEntry"},
        {8, &BSDCFG::LookupArpEntry, "LookupArpEntry"},
        {9, &BSDCFG::LookupArpEntry2, "LookupArpEntry2"},
        {10, &BSDCFG::ClearArpEntries, "ClearArpEntries"},
        {11, &BSDCFG::ClearArpEntries2, "ClearArpEntries2"},
        {12, &BSDCFG::PrintArpEntries, "PrintArpEntries"},
        {13, &BSDCFG::Cmd13, "Unknown13"},
        {14, &BSDCFG::Cmd14, "Unknown14"},
        {15, &BSDCFG::Cmd15, "Unknown15"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

BSDCFG::~BSDCFG() = default;

void BSDCFG::SetIfUp(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::SetIfUpWithEvent(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    auto* event = service_context.CreateEvent("BSDCFG:SetIfUpWithEvent");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(event->GetReadableEvent());
}

void BSDCFG::CancelIf(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::SetIfDown(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::GetIfState(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push<u32>(1); // Interface is up (stubbed)
}

void BSDCFG::DhcpRenew(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::AddStaticArpEntry(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::RemoveArpEntry(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::LookupArpEntry(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::LookupArpEntry2(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::ClearArpEntries(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::ClearArpEntries2(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::PrintArpEntries(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::Cmd13(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::Cmd14(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void BSDCFG::Cmd15(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

} // namespace Service::Sockets