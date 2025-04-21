// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

#include "common/logging/log.h"
#include "core/hle/service/nifm/nifm_utils.h"

namespace Service::NIFM::nn::nifm {

// Simple implementation to track network requests
namespace {
    std::mutex g_request_mutex;
    std::map<u32, NetworkRequest> g_requests;
    u32 g_next_request_id = 1;
    bool g_network_available = true; // Default to true for emulation
}

bool IsNetworkAvailable() {
    // For emulation purposes, we'll just return the mocked availability
    std::lock_guard lock(g_request_mutex);
    return g_network_available;
}

u32 SubmitNetworkRequest() {
    std::lock_guard lock(g_request_mutex);

    if (!g_network_available) {
        LOG_WARNING(Service_NIFM, "Network request submitted but network is not available");
    }

    u32 request_id = g_next_request_id++;

    NetworkRequest request{
        .request_id = request_id,
        .is_pending = true,
        .result = NetworkRequestResult::Success // Assume immediate success for emulation
    };

    g_requests[request_id] = request;

    LOG_INFO(Service_NIFM, "Network request submitted with ID: {}", request_id);
    return request_id;
}

NetworkRequestResult GetNetworkRequestResult(u32 request_id) {
    std::lock_guard lock(g_request_mutex);

    auto it = g_requests.find(request_id);
    if (it == g_requests.end()) {
        LOG_ERROR(Service_NIFM, "Tried to get result for invalid request ID: {}", request_id);
        return NetworkRequestResult::Error;
    }

    // For emulation, we'll mark the request as no longer pending once the result is checked
    it->second.is_pending = false;

    return it->second.result;
}

bool CancelNetworkRequest(u32 request_id) {
    std::lock_guard lock(g_request_mutex);

    auto it = g_requests.find(request_id);
    if (it == g_requests.end()) {
        LOG_ERROR(Service_NIFM, "Tried to cancel invalid request ID: {}", request_id);
        return false;
    }

    if (!it->second.is_pending) {
        LOG_WARNING(Service_NIFM, "Tried to cancel a request that is not pending, ID: {}", request_id);
        return false;
    }

    it->second.is_pending = false;
    it->second.result = NetworkRequestResult::Canceled;

    LOG_INFO(Service_NIFM, "Network request canceled with ID: {}", request_id);
    return true;
}

} // namespace Service::NIFM::nn::nifm
