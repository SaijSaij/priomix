// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Real power-state detection for supported wireless headsets through libheadsetcontrol
// (native C++ API, no CLI subprocess). A connected dongle alone does not mean the headset
// is on: BATTERY_UNAVAILABLE = dongle present but headset off, BATTERY_AVAILABLE /
// BATTERY_CHARGING = headset on. Proprietary 2.4 GHz protocols usually push nothing,
// hence polling with an interval chosen by the caller (AC vs. battery, see
// power_state.hpp).
//
// The ChatMix wheel is read through the same library (Headset::getChatmix(), verified in
// /usr/include/headsetcontrol/headsetcontrol.hpp of headsetcontrol 4.0). A wheel must
// react quickly, so it is polled on a separate, short interval, but only for the one
// headset the caller asks for (the device currently active for the game role).

#include <systemd/sd-event.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "event_timer.hpp"

namespace headsetcontrol {
class Headset;
}

namespace priomix {

struct HeadsetState {
    std::string usb_id;  // "usb:vvvv:pppp", same format as device_id.hpp
    std::string name;
    bool powered = true;
    bool supports_chatmix = false;
    std::optional<int> chatmix_level;  // 0..128, only while watched and readable
};

class HeadsetMonitor {
public:
    HeadsetMonitor(sd_event* event, std::function<void()> changed);
    ~HeadsetMonitor();

    // Sets the battery/presence poll interval and restarts the timer.
    void SetInterval(uint32_t ms);
    // Polls immediately (TriggerReevaluation), the regular timer keeps its schedule.
    void PollNow();

    // Enables fast wheel polling for this headset (empty = off).
    void WatchChatMix(const std::string& usb_id);

    const std::map<std::string, HeadsetState>& States() const { return states_; }
    const HeadsetState* Find(const std::string& usb_id) const;

    static constexpr uint32_t kChatMixPollMs = 250;

private:
    // Returns true if the state visible to the caller changed.
    bool Poll();
    bool PollChatMix();
    headsetcontrol::Headset* FindHeadset(const std::string& usb_id);

    std::function<void()> changed_;
    EventTimer poll_timer_;
    EventTimer chatmix_timer_;
    uint32_t interval_ms_ = 5000;
    std::string chatmix_usb_id_;
    std::vector<headsetcontrol::Headset> headsets_;
    std::map<std::string, HeadsetState> states_;
    bool warned_permission_ = false;
};

}  // namespace priomix
