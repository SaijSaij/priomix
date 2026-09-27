// SPDX-License-Identifier: GPL-3.0-only
#include "event_timer.hpp"

#include <cstring>
#include <ctime>

#include "logging.hpp"

namespace priomix {

EventTimer::EventTimer(sd_event* event, std::function<void()> callback)
    : callback_(std::move(callback)) {
    int r =
        sd_event_add_time_relative(event, &source_, CLOCK_MONOTONIC, UINT64_MAX / 2,
                                   1000 /* 1 ms accuracy */, &EventTimer::OnTimer, this);
    if (r < 0) {
        PRIOMIX_LOG_ERROR("Cannot create timer: %s", std::strerror(-r));
        source_ = nullptr;
        return;
    }
    sd_event_source_set_enabled(source_, SD_EVENT_OFF);
}

EventTimer::~EventTimer() {
    if (source_ != nullptr) sd_event_source_disable_unref(source_);
}

void EventTimer::Start(uint64_t ms) {
    if (source_ == nullptr) return;
    sd_event_source_set_time_relative(source_, ms * 1000);
    sd_event_source_set_enabled(source_, SD_EVENT_ONESHOT);
}

void EventTimer::StartIfIdle(uint64_t ms) {
    if (!IsPending()) Start(ms);
}

void EventTimer::Stop() {
    if (source_ != nullptr) sd_event_source_set_enabled(source_, SD_EVENT_OFF);
}

bool EventTimer::IsPending() const {
    if (source_ == nullptr) return false;
    int enabled = SD_EVENT_OFF;
    sd_event_source_get_enabled(source_, &enabled);
    return enabled != SD_EVENT_OFF;
}

int EventTimer::OnTimer(sd_event_source*, uint64_t, void* userdata) {
    auto* self = static_cast<EventTimer*>(userdata);
    // ONESHOT sources are disabled by sd-event before dispatch, so the callback may
    // re-arm.
    if (self->callback_) self->callback_();
    return 0;
}

}  // namespace priomix
