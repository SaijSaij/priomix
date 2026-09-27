// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// One-shot / re-armable monotonic timer on the sd-event main loop.

#include <systemd/sd-event.h>

#include <cstdint>
#include <functional>

namespace priomix {

class EventTimer {
public:
    EventTimer(sd_event* event, std::function<void()> callback);
    ~EventTimer();

    EventTimer(const EventTimer&) = delete;
    EventTimer& operator=(const EventTimer&) = delete;

    // (Re)arms the timer to fire once after `ms` milliseconds.
    void Start(uint64_t ms);
    // Arms the timer only if it is not already pending (debouncing).
    void StartIfIdle(uint64_t ms);
    void Stop();
    bool IsPending() const;

private:
    static int OnTimer(sd_event_source* source, uint64_t usec, void* userdata);

    sd_event_source* source_ = nullptr;
    std::function<void()> callback_;
};

}  // namespace priomix
