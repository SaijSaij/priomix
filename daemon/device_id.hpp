// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Derivation of stable device IDs from PipeWire node + device properties (hybrid scheme):
//   USB devices:       usb:<vendor>:<product>     (device.vendor.id / device.product.id)
//   Bluetooth devices: bt:<MAC>                   (api.bluez5.address, or parsed from
//                                                  device.name / node.name)
//   ALSA fallback:     alsa:<node.name without "alsa_" prefix>
//   anything else:     node:<node.name>
// The volatile node.name is only used where nothing more stable exists.

#include <map>
#include <string>

namespace priomix {

using Properties = std::map<std::string, std::string>;

struct DeviceIdentity {
    std::string id;            // primary stable ID
    std::string bus_path;      // device.bus-path, only consulted on collisions
    std::string usb_id;        // "usb:vvvv:pppp" for USB devices (headset matching)
    std::string display_name;  // informative only
};

// node_props: properties of the PipeWire node; device_props: properties of the PipeWire
// device the node belongs to (may be empty, e.g. for virtual sinks of other apps).
DeviceIdentity DeriveDeviceIdentity(const Properties& node_props,
                                    const Properties& device_props);

// "0x1038" / "1038" -> "1038" (lowercase, 4 digits); empty on parse failure.
std::string NormalizeUsbHexId(const std::string& value);

// Extracts a MAC address in "AA:BB:CC:DD:EE:FF" form from strings such as
// "bluez_card.AA_BB_CC_DD_EE_FF" or "bluez_output.AA_BB_CC_DD_EE_FF.1"; empty if none.
std::string ExtractBluetoothMac(const std::string& text);

}  // namespace priomix
