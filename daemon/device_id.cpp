// SPDX-License-Identifier: GPL-3.0-only
#include "device_id.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <regex>

namespace priomix {

namespace {

std::string Get(const Properties& props, const char* key) {
    auto it = props.find(key);
    return it != props.end() ? it->second : std::string();
}

// Device properties win over node properties; ALSA nodes carry only a subset of the
// device keys (verified with pw-dump: device.bus-path exists on the device only).
std::string GetAny(const Properties& node, const Properties& device, const char* key) {
    std::string value = Get(device, key);
    return value.empty() ? Get(node, key) : value;
}

}  // namespace

std::string NormalizeUsbHexId(const std::string& value) {
    if (value.empty()) return {};
    char* end = nullptr;
    unsigned long n = std::strtoul(value.c_str(), &end, 16);
    if (end == value.c_str() || *end != '\0' || n > 0xFFFF) return {};
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04lx", n);
    return buf;
}

std::string ExtractBluetoothMac(const std::string& text) {
    static const std::regex kMac(
        "([0-9A-Fa-f]{2})[_:]([0-9A-Fa-f]{2})[_:]([0-9A-Fa-f]{2})"
        "[_:]([0-9A-Fa-f]{2})[_:]([0-9A-Fa-f]{2})[_:]([0-9A-Fa-f]{2})");
    std::smatch match;
    if (!std::regex_search(text, match, kMac)) return {};
    std::string mac;
    for (size_t i = 1; i <= 6; ++i) {
        if (i > 1) mac += ':';
        for (char c : match[i].str()) mac += static_cast<char>(std::toupper(c));
    }
    return mac;
}

DeviceIdentity DeriveDeviceIdentity(const Properties& node, const Properties& device) {
    DeviceIdentity identity;
    const std::string node_name = Get(node, "node.name");
    const std::string api = GetAny(node, device, "device.api");
    const std::string bus = GetAny(node, device, "device.bus");
    identity.bus_path = GetAny(node, device, "device.bus-path");

    identity.display_name = Get(node, "node.description");
    if (identity.display_name.empty())
        identity.display_name = Get(device, "device.description");
    if (identity.display_name.empty()) identity.display_name = node_name;

    // PCI cards carry device.vendor.id too, so the bus must be checked explicitly.
    if (bus == "usb") {
        std::string vendor = NormalizeUsbHexId(GetAny(node, device, "device.vendor.id"));
        std::string product =
            NormalizeUsbHexId(GetAny(node, device, "device.product.id"));
        if (!vendor.empty() && !product.empty()) {
            identity.usb_id = "usb:" + vendor + ":" + product;
            identity.id = identity.usb_id;
            return identity;
        }
    }

    if (api == "bluez5") {
        std::string mac = GetAny(node, device, "api.bluez5.address");
        mac =
            ExtractBluetoothMac(mac.empty() ? GetAny(node, device, "device.name") : mac);
        if (mac.empty()) mac = ExtractBluetoothMac(node_name);
        if (!mac.empty()) {
            identity.id = "bt:" + mac;
            return identity;
        }
    }

    if (api == "alsa" && node_name.rfind("alsa_", 0) == 0) {
        identity.id = "alsa:" + node_name.substr(5);
        return identity;
    }

    identity.id = "node:" + node_name;
    return identity;
}

}  // namespace priomix
