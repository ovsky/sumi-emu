// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"

namespace Service::NIM {

// Network installation task status
enum class TaskStatus {
    None = 0,
    Pending = 1,
    Downloading = 2,
    Installing = 3,
    Complete = 4,
    Failed = 5,
    Canceled = 6,
};

// Network installation task progress
struct TaskProgress {
    u64 downloaded_bytes;
    u64 total_bytes;
    TaskStatus status;
};

// Network installation task
struct Task {
    u64 task_id;
    TaskProgress progress;
};

namespace nn::nim {

// Checks if the NIM service is available
bool IsServiceAvailable();

// Creates a new installation task
// Returns the task ID or 0 if the task creation failed
u64 CreateInstallTask(u64 application_id);

// Gets the progress of an installation task
// Returns the task progress
TaskProgress GetTaskProgress(u64 task_id);

// Starts an installation task
// Returns true if the task was successfully started
bool StartInstallTask(u64 task_id);

// Cancels an installation task
// Returns true if the task was successfully canceled
bool CancelInstallTask(u64 task_id);

} // namespace nn::nim

} // namespace Service::NIM