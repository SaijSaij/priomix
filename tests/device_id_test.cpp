// SPDX-License-Identifier: GPL-3.0-only
#include "device_id.hpp"

#include <gtest/gtest.h>

namespace priomix {
namespace {

TEST(DeviceId, UsbDeviceUsesVendorAndProduct) {
    Properties node = {
        {"node.name", "alsa_output.usb-SteelSeries_Arctis_Nova_7-00.analog-stereo"},
        {"node.description", "Arctis Nova 7 Analog Stereo"},
        {"device.api", "alsa"}};
    Properties device = {{"device.bus", "usb"},
                         {"device.vendor.id", "0x1038"},
                         {"device.product.id", "0x2202"},
                         {"device.bus-path", "pci-0000:00:14.0-usb-0:2:1.0"}};
    DeviceIdentity id = DeriveDeviceIdentity(node, device);
    EXPECT_EQ(id.id, "usb:1038:2202");
    EXPECT_EQ(id.usb_id, "usb:1038:2202");
    EXPECT_EQ(id.bus_path, "pci-0000:00:14.0-usb-0:2:1.0");
    EXPECT_EQ(id.display_name, "Arctis Nova 7 Analog Stereo");
}

TEST(DeviceId, PciCardWithVendorIdIsNotUsb) {
    // Verified with pw-dump: PCI cards carry device.vendor.id / device.product.id too.
    Properties node = {{"node.name", "alsa_output.pci-0000_02_02.0.analog-stereo"},
                       {"device.api", "alsa"}};
    Properties device = {{"device.bus", "pci"},
                         {"device.vendor.id", "0x1274"},
                         {"device.product.id", "0x1371"}};
    DeviceIdentity id = DeriveDeviceIdentity(node, device);
    EXPECT_EQ(id.id, "alsa:output.pci-0000_02_02.0.analog-stereo");
    EXPECT_TRUE(id.usb_id.empty());
}

TEST(DeviceId, BluetoothFromAddressOrName) {
    Properties node = {{"node.name", "bluez_output.AA_BB_CC_DD_EE_0F.1"},
                       {"device.api", "bluez5"}};
    EXPECT_EQ(DeriveDeviceIdentity(node, {}).id, "bt:AA:BB:CC:DD:EE:0F");
    Properties device = {{"api.bluez5.address", "11:22:33:44:55:66"}};
    EXPECT_EQ(DeriveDeviceIdentity(node, device).id, "bt:11:22:33:44:55:66");
    Properties card = {{"device.name", "bluez_card.aa_bb_cc_dd_ee_ff"}};
    Properties anon = {{"node.name", "whatever"}, {"device.api", "bluez5"}};
    EXPECT_EQ(DeriveDeviceIdentity(anon, card).id, "bt:AA:BB:CC:DD:EE:FF");
}

TEST(DeviceId, OtherNodesFallBackToNodeName) {
    Properties node = {{"node.name", "easyeffects_sink"}};
    EXPECT_EQ(DeriveDeviceIdentity(node, {}).id, "node:easyeffects_sink");
}

TEST(DeviceId, NormalizeUsbHexId) {
    EXPECT_EQ(NormalizeUsbHexId("0x1038"), "1038");
    EXPECT_EQ(NormalizeUsbHexId("12AD"), "12ad");
    EXPECT_EQ(NormalizeUsbHexId("0x1"), "0001");
    EXPECT_EQ(NormalizeUsbHexId("zz"), "");
    EXPECT_EQ(NormalizeUsbHexId("0x12345"), "");
}

}  // namespace
}  // namespace priomix
