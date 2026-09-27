// SPDX-License-Identifier: GPL-3.0-only
#include "headset_monitor.hpp"

#include <headsetcontrol.hpp>

#include <cstdio>

#include "logging.hpp"

namespace priomix {

namespace {

// HID reads block the (single threaded) main loop, keep them short. A powered-off Nova 7
// answers immediately with BATTERY_UNAVAILABLE, the timeout only matters for broken
// devices.
constexpr int kHidTimeoutMs = 1000;

std::string UsbId(uint16_t vendor, uint16_t product) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "usb:%04x:%04x", vendor, product);
    return buf;
}

}  // namespace

HeadsetMonitor::HeadsetMonitor(sd_event* event, std::function<void()> changed)
    : changed_(std::move(changed)),
      poll_timer_(event,
                  [this] {
                      if (Poll() && changed_) changed_();
                      poll_timer_.Start(interval_ms_);
                  }),
      chatmix_timer_(event, [this] {
          if (PollChatMix() && changed_) changed_();
          if (!chatmix_usb_id_.empty()) chatmix_timer_.Start(kChatMixPollMs);
      }) {
    headsetcontrol::setDeviceTimeout(kHidTimeoutMs);
    PRIOMIX_LOG_DEBUG("Using HeadsetControl %.*s",
                      static_cast<int>(headsetcontrol::version().size()),
                      headsetcontrol::version().data());
}

HeadsetMonitor::~HeadsetMonitor() = default;

void HeadsetMonitor::SetInterval(uint32_t ms) {
    if (ms == interval_ms_ && poll_timer_.IsPending()) return;
    interval_ms_ = ms;
    PRIOMIX_LOG_DEBUG("Headset poll interval: %u ms", ms);
    poll_timer_.Start(interval_ms_);
}

void HeadsetMonitor::PollNow() {
    Poll();
    PollChatMix();
}

void HeadsetMonitor::WatchChatMix(const std::string& usb_id) {
    if (usb_id == chatmix_usb_id_) return;
    // Stop reporting a wheel value for the previously watched headset.
    if (auto it = states_.find(chatmix_usb_id_); it != states_.end()) {
        it->second.chatmix_level.reset();
    }
    chatmix_usb_id_ = usb_id;
    if (usb_id.empty()) {
        chatmix_timer_.Stop();
    } else {
        PollChatMix();
        chatmix_timer_.Start(kChatMixPollMs);
    }
}

const HeadsetState* HeadsetMonitor::Find(const std::string& usb_id) const {
    auto it = states_.find(usb_id);
    return it != states_.end() ? &it->second : nullptr;
}

headsetcontrol::Headset* HeadsetMonitor::FindHeadset(const std::string& usb_id) {
    for (auto& headset : headsets_) {
        if (UsbId(headset.vendorId(), headset.productId()) == usb_id) return &headset;
    }
    return nullptr;
}

bool HeadsetMonitor::Poll() {
    // Re-discover every cycle: dongles come and go, and discovery is only a HID
    // enumeration. Headset objects are kept until the next cycle for the wheel polling.
    headsets_ = headsetcontrol::discover();
    PRIOMIX_LOG_DEBUG("Headset poll: %zu device(s) found", headsets_.size());

    std::map<std::string, HeadsetState> states;
    for (auto& headset : headsets_) {
        HeadsetState state;
        state.usb_id = UsbId(headset.vendorId(), headset.productId());
        state.name = std::string(headset.name());
        state.supports_chatmix = headset.supports(CAP_CHATMIX_STATUS);

        if (headset.supports(CAP_BATTERY_STATUS)) {
            auto battery = headset.getBattery();
            if (battery) {
                // Only an explicit "dongle there, headset off" marks it unavailable.
                state.powered = battery->status != BATTERY_UNAVAILABLE;
            } else {
                const auto& error = battery.error();
                if (error.code == headsetcontrol::DeviceError::Code::DeviceOffline) {
                    state.powered = false;
                } else {
                    // Fail open: a permission or protocol problem must not hide a device.
                    if (error.code ==
                            headsetcontrol::DeviceError::Code::PermissionDenied &&
                        !warned_permission_) {
                        PRIOMIX_LOG_WARN(
                            "No permission to access '%s' (udev rules of "
                            "headsetcontrol installed?)",
                            state.name.c_str());
                        warned_permission_ = true;
                    }
                    PRIOMIX_LOG_DEBUG("Battery query for '%s' failed: %s",
                                      state.name.c_str(), error.message.c_str());
                    auto previous = states_.find(state.usb_id);
                    state.powered = previous == states_.end() || previous->second.powered;
                }
            }
        }
        if (auto previous = states_.find(state.usb_id); previous != states_.end()) {
            state.chatmix_level = previous->second.chatmix_level;
        }
        PRIOMIX_LOG_DEBUG("Headset '%s': %s", state.name.c_str(),
                          state.powered ? "on" : "off");
        states[state.usb_id] = std::move(state);
    }

    auto summary = [](const std::map<std::string, HeadsetState>& s) {
        std::string out;
        for (const auto& [id, st] : s) out += id + (st.powered ? "=on;" : "=off;");
        return out;
    };
    bool changed = summary(states) != summary(states_);
    for (const auto& [id, state] : states) {
        auto previous = states_.find(id);
        if (previous == states_.end() || previous->second.powered != state.powered) {
            PRIOMIX_LOG_INFO("Headset '%s' is %s", state.name.c_str(),
                             state.powered ? "on" : "off");
        }
    }
    states_ = std::move(states);
    return changed;
}

bool HeadsetMonitor::PollChatMix() {
    if (chatmix_usb_id_.empty()) return false;
    auto state = states_.find(chatmix_usb_id_);
    if (state == states_.end()) return false;

    std::optional<int> level;
    auto* headset = FindHeadset(chatmix_usb_id_);
    if (headset != nullptr && state->second.powered && state->second.supports_chatmix) {
        auto result = headset->getChatmix();
        if (result) level = result->level;
    }
    if (level == state->second.chatmix_level) return false;
    state->second.chatmix_level = level;
    return true;
}

}  // namespace priomix
