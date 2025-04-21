// SPDX-FileCopyrightText: Copyright 2025 sumi Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <map>
#include <mutex>

#include "common/logging/log.h"
#include "core/hle/service/nim/nim_utils.h"

namespace Service::NIM::nn::nim {

// Simple implementation to track installation tasks
namespace {
    std::mutex g_task_mutex;
    std::map<u64, Task> g_tasks;
    u64 g_next_task_id = 1;
    bool g_service_available = true; // Default to true for emulation
}

bool IsServiceAvailable() {
    std::lock_guard lock(g_task_mutex);
    return g_service_available;
}

u64 CreateInstallTask(u64 application_id) {
    std::lock_guard lock(g_task_mutex);

    if (!g_service_available) {
        LOG_WARNING(Service_NIM, "Installation task creation attempted but service is not available");
        return 0;
    }

    u64 task_id = g_next_task_id++;

    Task task{
        .task_id = task_id,
        .progress = {
            .downloaded_bytes = 0,
            .total_bytes = 1'000'000'000, // Fake 1GB download size
            .status = TaskStatus::None
        }
    };

    g_tasks[task_id] = task;

    LOG_INFO(Service_NIM, "Installation task created for application 0x{:016X} with ID: {}",
             application_id, task_id);
    return task_id;
}

TaskProgress GetTaskProgress(u64 task_id) {
    std::lock_guard lock(g_task_mutex);

    auto it = g_tasks.find(task_id);
    if (it == g_tasks.end()) {
        LOG_ERROR(Service_NIM, "Tried to get progress for invalid task ID: {}", task_id);
        return {0, 0, TaskStatus::Failed};
    }

    // If task is in download state, simulate progress
    if (it->second.progress.status == TaskStatus::Downloading) {
        // Simulate download progress (add 10% of total size)
        auto& progress = it->second.progress;
        const u64 increment = progress.total_bytes / 10;

        progress.downloaded_bytes += increment;
        if (progress.downloaded_bytes >= progress.total_bytes) {
            progress.downloaded_bytes = progress.total_bytes;
            progress.status = TaskStatus::Installing;
            LOG_INFO(Service_NIM, "Task ID {} download complete, now installing", task_id);
        }
    } else if (it->second.progress.status == TaskStatus::Installing) {
        // Simulate installation completion
        it->second.progress.status = TaskStatus::Complete;
        LOG_INFO(Service_NIM, "Task ID {} installation complete", task_id);
    }

    return it->second.progress;
}

bool StartInstallTask(u64 task_id) {
    std::lock_guard lock(g_task_mutex);

    auto it = g_tasks.find(task_id);
    if (it == g_tasks.end()) {
        LOG_ERROR(Service_NIM, "Tried to start invalid task ID: {}", task_id);
        return false;
    }

    if (it->second.progress.status != TaskStatus::None &&
        it->second.progress.status != TaskStatus::Pending) {
        LOG_WARNING(Service_NIM, "Tried to start task ID {} which is already in progress", task_id);
        return false;
    }

    it->second.progress.status = TaskStatus::Downloading;
    LOG_INFO(Service_NIM, "Started installation task ID: {}", task_id);

    return true;
}

bool CancelInstallTask(u64 task_id) {
    std::lock_guard lock(g_task_mutex);

    auto it = g_tasks.find(task_id);
    if (it == g_tasks.end()) {
        LOG_ERROR(Service_NIM, "Tried to cancel invalid task ID: {}", task_id);
        return false;
    }

    if (it->second.progress.status == TaskStatus::Complete ||
        it->second.progress.status == TaskStatus::Failed ||
        it->second.progress.status == TaskStatus::Canceled) {
        LOG_WARNING(Service_NIM, "Tried to cancel task ID {} which is already in a final state", task_id);
        return false;
    }

    it->second.progress.status = TaskStatus::Canceled;
    LOG_INFO(Service_NIM, "Canceled installation task ID: {}", task_id);

    return true;
}

} // namespace Service::NIM::nn::nim
