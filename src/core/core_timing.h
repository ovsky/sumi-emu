// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>


#include "common/common_types.h"
#include "common/thread.h"
#include "common/wall_clock.h"

namespace Core::Timing {

/// A callback that may be scheduled for a particular core timing event.
using TimedCallback = std::function<std::optional<std::chrono::nanoseconds>(
    s64 time, std::chrono::nanoseconds ns_late)>;

/// Contains the characteristics of a particular event.
struct EventType {
    explicit EventType(TimedCallback&& callback_, std::string&& name_)
        : callback{std::move(callback_)}, name{std::move(name_)}, sequence_number{0} {}

    /// The event's callback function.
    TimedCallback callback;
    /// A pointer to the name of the event.
    const std::string name;
    /// A monotonic sequence number, incremented when this event is
    /// changed externally.
    size_t sequence_number;
};

enum class UnscheduleEventType {
    Wait,
    NoWait,
};

class CoreTiming {
public:
    CoreTiming();
    ~CoreTiming();

    CoreTiming(const CoreTiming&) = delete;
    CoreTiming(CoreTiming&&) = delete;

    CoreTiming& operator=(const CoreTiming&) = delete;
    CoreTiming& operator=(CoreTiming&&) = delete;

    void Initialize(std::function<void()>&& on_thread_init_);
    void ClearPendingEvents();
    void SetMulticore(bool is_multicore_) {
        is_multicore = is_multicore_;
    }
    void Pause(bool is_paused);
    void SyncPause(bool is_paused);
    bool IsRunning() const;
    bool HasStarted() const {
        return has_started;
    }
    bool HasPendingEvents() const;
    void ScheduleEvent(std::chrono::nanoseconds ns_into_future,
                       const std::shared_ptr<EventType>& event_type, bool absolute_time = false);
    void ScheduleLoopingEvent(std::chrono::nanoseconds start_time,
                              std::chrono::nanoseconds resched_time,
                              const std::shared_ptr<EventType>& event_type,
                              bool absolute_time = false);
    void UnscheduleEvent(const std::shared_ptr<EventType>& event_type,
                         UnscheduleEventType type = UnscheduleEventType::Wait);
    void AddTicks(u64 ticks_to_add);
    void ResetTicks();
    void Idle();
    s64 GetDowncount() const {
        return downcount.load(std::memory_order_relaxed);
    }
    u64 GetClockTicks() const;
    u64 GetGPUTicks() const;
    std::chrono::microseconds GetGlobalTimeUs() const;
    std::chrono::nanoseconds GetGlobalTimeNs() const;
    std::optional<s64> Advance();

private:
    struct Event {
        s64 time;
        u64 fifo_order;
        std::shared_ptr<EventType> type;
        bool operator>(const Event& other) const {
            return std::tie(time, fifo_order) > std::tie(other.time, other.fifo_order);
        }
    };

    static void ThreadEntry(CoreTiming& instance);
    void ThreadLoop();
    void Reset();

    std::unique_ptr<Common::WallClock> clock;
    std::atomic<s64> global_timer{0};
    std::vector<Event> event_queue;
    std::atomic<u64> event_fifo_id{0};

    Common::Event event{};
    Common::Event pause_event{};
    mutable std::mutex basic_lock;
    std::mutex advance_lock;
    std::unique_ptr<std::jthread> timer_thread;
    std::atomic<bool> paused{};
    std::atomic<bool> paused_set{};
    std::atomic<bool> wait_set{};
    std::atomic<bool> shutting_down{};
    std::atomic<bool> has_started{};
    std::function<void()> on_thread_init{};

    bool is_multicore{};
    std::atomic<s64> pause_end_time{};

    std::atomic<u64> cpu_ticks{};
    std::atomic<s64> downcount{};
};

std::shared_ptr<EventType> CreateEvent(std::string name, TimedCallback&& callback);

} // namespace Core::Timing
