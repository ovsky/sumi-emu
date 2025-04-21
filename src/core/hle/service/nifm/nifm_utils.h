// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"

namespace Service::NIFM {

// Network request result codes
enum class NetworkRequestResult {
    Success = 0,
    Error = 1,
    Canceled = 2,
    Timeout = 3,
};

// Network request structure
struct NetworkRequest {
    u32 request_id;
    bool is_pending;
    NetworkRequestResult result;
};

namespace nn::nifm {

// Checks if network connectivity is available
bool IsNetworkAvailable();

// Submits a network connection request
// Returns the request ID or 0 if the request failed
u32 SubmitNetworkRequest();

// Gets the status of a network request
// Returns the request result
NetworkRequestResult GetNetworkRequestResult(u32 request_id);

// Cancels a pending network request
// Returns true if the request was successfully canceled
bool CancelNetworkRequest(u32 request_id);

} // namespace nn::nifm

} // namespace Service::NIFM
