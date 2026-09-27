// SPDX-License-Identifier: GPL-3.0-only
#include "power_state.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "logging.hpp"

namespace priomix {

namespace {

constexpr const char* kUPowerService = "org.freedesktop.UPower";
constexpr const char* kUPowerPath = "/org/freedesktop/UPower";
constexpr const char* kUPowerInterface = "org.freedesktop.UPower";
constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";

}  // namespace

PowerState::PowerState(std::function<void(bool)> changed)
    : changed_(std::move(changed)) {}

PowerState::~PowerState() {
    proxy_.reset();
    if (connection_) connection_->detachSdEventLoop();
}

void PowerState::Start(sd_event* event) {
    try {
        connection_ = sdbus::createSystemBusConnection();
        connection_->attachSdEventLoop(event);
        proxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName{kUPowerService},
                                    sdbus::ObjectPath{kUPowerPath});

        proxy_->uponSignal("PropertiesChanged")
            .onInterface(kPropertiesInterface)
            .call([this](const std::string& interface,
                         const std::map<std::string, sdbus::Variant>& changed,
                         const std::vector<std::string>& invalidated) {
                if (interface != kUPowerInterface) return;
                try {
                    if (auto it = changed.find("OnBattery"); it != changed.end()) {
                        Update(it->second.get<bool>());
                    } else if (std::find(invalidated.begin(), invalidated.end(),
                                         "OnBattery") != invalidated.end()) {
                        Update(proxy_->getProperty("OnBattery")
                                   .onInterface(kUPowerInterface)
                                   .get<bool>());
                    }
                } catch (const sdbus::Error& e) {
                    PRIOMIX_LOG_WARN("Cannot read UPower OnBattery: %s", e.what());
                }
            });

        on_battery_ =
            proxy_->getProperty("OnBattery").onInterface(kUPowerInterface).get<bool>();
        PRIOMIX_LOG_INFO("Power source: %s", on_battery_ ? "battery" : "AC");
    } catch (const sdbus::Error& e) {
        PRIOMIX_LOG_WARN("UPower not available (%s), assuming AC power", e.what());
        on_battery_ = false;
    }
}

void PowerState::Update(bool on_battery) {
    if (on_battery == on_battery_) return;
    on_battery_ = on_battery;
    PRIOMIX_LOG_INFO("Power source changed: %s", on_battery ? "battery" : "AC");
    if (changed_) changed_(on_battery);
}

}  // namespace priomix
