// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// AC/battery detection via UPower (org.freedesktop.UPower, property OnBattery). UPower
// lives on the *system* bus, so this uses its own sdbus-c++ connection, attached to the
// same sd-event loop as everything else. Event based via PropertiesChanged; the state is
// queried once at start. Without UPower the daemon assumes AC power.

#include <systemd/sd-event.h>

#include <functional>
#include <memory>

namespace sdbus {
class IConnection;
class IProxy;
}  // namespace sdbus

namespace priomix {

class PowerState {
public:
    explicit PowerState(std::function<void(bool on_battery)> changed);
    ~PowerState();

    void Start(sd_event* event);
    bool OnBattery() const { return on_battery_; }

private:
    void Update(bool on_battery);

    std::function<void(bool)> changed_;
    std::unique_ptr<sdbus::IConnection> connection_;
    std::unique_ptr<sdbus::IProxy> proxy_;
    bool on_battery_ = false;
};

}  // namespace priomix
