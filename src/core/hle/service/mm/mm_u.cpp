// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <vector>
#include "common/logging/log.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/mm/mm_u.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/sm/sm.h"

namespace Service::MM {

class MM_U final : public ServiceFramework<MM_U> {
public:
    explicit MM_U(Core::System& system_) : ServiceFramework{system_, "mm:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &MM_U::InitializeOld, "InitializeOld"},
            {1, &MM_U::FinalizeOld, "FinalizeOld"},
            {2, &MM_U::SetAndWaitOld, "SetAndWaitOld"},
            {3, &MM_U::GetOld, "GetOld"},
            {4, &MM_U::Initialize, "Initialize"},
            {5, &MM_U::Finalize, "Finalize"},
            {6, &MM_U::SetAndWait, "SetAndWait"},
            {7, &MM_U::Get, "Get"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

private:
    void InitializeOld(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto module = rp.PopEnum<Module>();
        [[maybe_unused]] const auto priority = rp.Pop<u32>();
        const auto clear_mode = static_cast<EventClearMode>(rp.Pop<u32>());

        LOG_DEBUG(Service_MM, "called. module={:d}, clear_mode={:d}",
                 static_cast<u32>(module), static_cast<u32>(clear_mode));

        sessions.emplace_back(module, next_request_id++, clear_mode);

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void FinalizeOld(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto module = rp.PopEnum<Module>();

        LOG_DEBUG(Service_MM, "called. module={:d}", static_cast<u32>(module));

        std::erase_if(sessions, [&module](const auto& session) {
            return session.GetModule() == module;
        });

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetAndWaitOld(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto module = rp.PopEnum<Module>();
        const auto min = rp.Pop<u32>();
        const auto max = rp.Pop<s32>();

        LOG_DEBUG(Service_MM, "called. module={:d}, min=0x{:X}, max=0x{:X}",
                 static_cast<u32>(module), min, max);

        if (auto it = std::find_if(sessions.begin(), sessions.end(),
                                 [&module](const auto& session) {
                                     return session.GetModule() == module;
                                 });
            it != sessions.end()) {
            it->SetAndWait(min, max);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetOld(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto module = rp.PopEnum<Module>();

        LOG_DEBUG(Service_MM, "called. module={:d}", static_cast<u32>(module));

        u32 current_min = 0;
        if (auto it = std::find_if(sessions.begin(), sessions.end(),
                                 [&module](const auto& session) {
                                     return session.GetModule() == module;
                                 });
            it != sessions.end()) {
            current_min = it->GetMin();
        }

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(current_min);
    }

    void Initialize(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto module = rp.PopEnum<Module>();
        [[maybe_unused]] const auto priority = rp.Pop<u32>();
        const auto clear_mode = static_cast<EventClearMode>(rp.Pop<u32>());

        LOG_DEBUG(Service_MM, "called. module={:d}, clear_mode={:d}",
                 static_cast<u32>(module), static_cast<u32>(clear_mode));

        const u32 current_id = next_request_id++;
        sessions.emplace_back(module, current_id, clear_mode);

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(current_id);
    }

    void Finalize(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request_id = rp.Pop<u32>();

        LOG_DEBUG(Service_MM, "called. request_id={:d}", request_id);

        std::erase_if(sessions, [&request_id](const auto& session) {
            return session.GetRequestId() == request_id;
        });

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SetAndWait(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request_id = rp.Pop<u32>();
        const auto min = rp.Pop<u32>();
        const auto max = rp.Pop<s32>();

        LOG_DEBUG(Service_MM, "called. request_id={:d}, min=0x{:X}, max=0x{:X}",
                 request_id, min, max);

        if (auto it = std::find_if(sessions.begin(), sessions.end(),
                                 [&request_id](const auto& session) {
                                     return session.GetRequestId() == request_id;
                                 });
            it != sessions.end()) {
            it->SetAndWait(min, max);
        }

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Get(HLERequestContext& ctx) {
        IPC::RequestParser rp{ctx};
        const auto request_id = rp.Pop<u32>();

        LOG_DEBUG(Service_MM, "called. request_id={:d}", request_id);

        u32 current_min = 0;
        if (auto it = std::find_if(sessions.begin(), sessions.end(),
                                 [&request_id](const auto& session) {
                                     return session.GetRequestId() == request_id;
                                 });
            it != sessions.end()) {
            current_min = it->GetMin();
        }

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(current_min);
    }

    std::vector<Session> sessions;
    u32 next_request_id{1};
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);
    server_manager->RegisterNamedService("mm:u", std::make_shared<MM_U>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::MM
