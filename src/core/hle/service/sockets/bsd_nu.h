// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"
#include "core/hle/service/kernel_helpers.h"

namespace Core {
class System;
}

namespace Service::Sockets {

class ISfUserService final : public ServiceFramework<ISfUserService> {
public:
    explicit ISfUserService(Core::System& system_);
    ~ISfUserService() override;

private:
    void Assign(HLERequestContext& ctx);
    void GetUserInfo(HLERequestContext& ctx);
    void GetStateChangedEvent(HLERequestContext& ctx);

    KernelHelpers::ServiceContext service_context;
};

class ISfAssignedNetworkInterfaceService final : public ServiceFramework<ISfAssignedNetworkInterfaceService> {
public:
    explicit ISfAssignedNetworkInterfaceService(Core::System& system_);
    ~ISfAssignedNetworkInterfaceService() override;

private:
    void AddSession(HLERequestContext& ctx);
};

class BSD_NU final : public ServiceFramework<BSD_NU> {
public:
    explicit BSD_NU(Core::System& system_);
    ~BSD_NU() override;

private:
    void CreateUserService(HLERequestContext& ctx);
};

} // namespace Service::Sockets