// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/sockets/bsd_nu.h"

namespace Service::Sockets {

ISfUserService::ISfUserService(Core::System& system_)
    : ServiceFramework{system_, "ISfUserService"},
      service_context{system_, "ISfUserService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ISfUserService::Assign, "Assign"},
        {128, &ISfUserService::GetUserInfo, "GetUserInfo"},
        {129, &ISfUserService::GetStateChangedEvent, "GetStateChangedEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISfUserService::~ISfUserService() = default;

void ISfUserService::Assign(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ISfAssignedNetworkInterfaceService>(system);
}

void ISfUserService::GetUserInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

void ISfUserService::GetStateChangedEvent(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    auto* event = service_context.CreateEvent("ISfUserService:StateChanged");

    IPC::ResponseBuilder rb{ctx, 2, 1};
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(event->GetReadableEvent());
}

ISfAssignedNetworkInterfaceService::ISfAssignedNetworkInterfaceService(Core::System& system_)
    : ServiceFramework{system_, "ISfAssignedNetworkInterfaceService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ISfAssignedNetworkInterfaceService::AddSession, "AddSession"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISfAssignedNetworkInterfaceService::~ISfAssignedNetworkInterfaceService() = default;

void ISfAssignedNetworkInterfaceService::AddSession(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called");

    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

BSD_NU::BSD_NU(Core::System& system_) : ServiceFramework{system_, "bsd:nu"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &BSD_NU::CreateUserService, "CreateUserService"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

BSD_NU::~BSD_NU() = default;

void BSD_NU::CreateUserService(HLERequestContext& ctx) {
    LOG_DEBUG(Service, "called");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ISfUserService>(system);
}

} // namespace Service::Sockets