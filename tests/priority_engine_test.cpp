// SPDX-License-Identifier: GPL-3.0-only
#include "priority_engine.hpp"

#include <gtest/gtest.h>

namespace priomix {
namespace {

PriorityList List(std::initializer_list<PriorityEntry> entries) {
    return PriorityList(entries);
}

TEST(ResolveKeys, NewDeviceIsInsertedAtFrontOfAllLists) {
    PriorityList game = List({{"alsa:internal", ""}});
    PriorityList chat = List({{"alsa:internal", ""}});
    std::vector<std::string> ignored;
    std::vector<KeyRequest> devices = {{"usb:1038:12ad", "pci-usb-2", "n1", {}},
                                       {"alsa:internal", "", "n2", {}}};

    EXPECT_TRUE(ResolveKeys(devices, {&game, &chat}, {&ignored}));
    EXPECT_EQ(devices[0].key, "usb:1038:12ad");
    EXPECT_EQ(devices[1].key, "alsa:internal");
    ASSERT_EQ(game.size(), 2u);
    EXPECT_EQ(game[0].id, "usb:1038:12ad");
    EXPECT_TRUE(game[0].disambiguation_path.empty());
    EXPECT_EQ(chat[0].id, "usb:1038:12ad");
}

TEST(ResolveKeys, KnownDevicesDoNotModifyLists) {
    PriorityList game = List({{"a", ""}, {"b", ""}});
    std::vector<std::string> ignored;
    std::vector<KeyRequest> devices = {{"b", "", "nb", {}}};
    EXPECT_FALSE(ResolveKeys(devices, {&game}, {&ignored}));
    EXPECT_EQ(devices[0].key, "b");
}

TEST(ResolveKeys, CollisionTakesOverExistingEntryAndAddsSecond) {
    PriorityList game = List({{"alsa:internal", ""}, {"usb:1:2", ""}});
    std::vector<std::string> ignored = {"usb:1:2"};
    std::vector<KeyRequest> devices = {{"usb:1:2", "port-a", "n1", {}},
                                       {"usb:1:2", "port-b", "n2", {}}};

    EXPECT_TRUE(ResolveKeys(devices, {&game}, {&ignored}));
    EXPECT_EQ(devices[0].key, "usb:1:2@port-a");
    EXPECT_EQ(devices[1].key, "usb:1:2@port-b");
    // The existing entry keeps its position and gets the path, the second device is new.
    ASSERT_EQ(game.size(), 3u);
    EXPECT_EQ(game[0], (PriorityEntry{"usb:1:2", "port-b"}));
    EXPECT_EQ(game[1], (PriorityEntry{"alsa:internal", ""}));
    EXPECT_EQ(game[2], (PriorityEntry{"usb:1:2", "port-a"}));
    // Ignore list follows the rename.
    EXPECT_EQ(ignored, std::vector<std::string>{"usb:1:2@port-a"});
}

TEST(ResolveKeys, CollisionMatchesStoredPaths) {
    PriorityList game = List({{"usb:1:2", "port-b"}, {"usb:1:2", "port-a"}});
    std::vector<std::string> ignored;
    std::vector<KeyRequest> devices = {{"usb:1:2", "port-a", "n1", {}},
                                       {"usb:1:2", "port-b", "n2", {}}};
    EXPECT_FALSE(ResolveKeys(devices, {&game}, {&ignored}));
    EXPECT_EQ(devices[0].key, "usb:1:2@port-a");
    EXPECT_EQ(devices[1].key, "usb:1:2@port-b");
}

TEST(ResolveKeys, SingleDevicePrefersMatchingPath) {
    PriorityList game = List({{"usb:1:2", "port-a"}, {"usb:1:2", "port-b"}});
    std::vector<std::string> ignored;
    std::vector<KeyRequest> devices = {{"usb:1:2", "port-b", "n", {}}};
    EXPECT_FALSE(ResolveKeys(devices, {&game}, {&ignored}));
    EXPECT_EQ(devices[0].key, "usb:1:2@port-b");
}

TEST(ResolveKeys, SameBusPathFallsBackToNodeName) {
    PriorityList game;
    std::vector<std::string> ignored;
    std::vector<KeyRequest> devices = {{"usb:1:2", "p", "alsa_output.x.game", {}},
                                       {"usb:1:2", "p", "alsa_output.x.chat", {}}};
    EXPECT_TRUE(ResolveKeys(devices, {&game}, {&ignored}));
    EXPECT_EQ(devices[0].key, "usb:1:2@alsa_output.x.game");
    EXPECT_EQ(devices[1].key, "usb:1:2@alsa_output.x.chat");
}

TEST(PickWinner, SkipsIgnoredAndUnavailable) {
    PriorityList list = List({{"a", ""}, {"b", ""}, {"c", ""}, {"d", ""}});
    auto available = [](const std::string& k) { return k != "b"; };
    EXPECT_EQ(PickWinner(list, {"a"}, available), "c");
    EXPECT_EQ(PickWinner(list, {}, available), "a");
    EXPECT_EQ(PickWinner(list, {"a", "c", "d"}, available), std::nullopt);
}

TEST(Reorder, FollowsKeysKeepsMissingIgnoresUnknown) {
    PriorityList list = List({{"a", ""}, {"b", ""}, {"c", "p"}});
    PriorityList result = Reorder(list, {"c@p", "unknown", "a", "a"});
    EXPECT_EQ(Keys(result), (std::vector<std::string>{"c@p", "a", "b"}));
}

TEST(ChatMix, DampeningIsAsymmetric) {
    EXPECT_EQ(ComputeDampening(50).game, 100u);
    EXPECT_EQ(ComputeDampening(50).chat, 100u);
    EXPECT_EQ(ComputeDampening(75).game, 100u);
    EXPECT_EQ(ComputeDampening(75).chat, 50u);
    EXPECT_EQ(ComputeDampening(100).chat, 0u);
    EXPECT_EQ(ComputeDampening(0).game, 0u);
    EXPECT_EQ(ComputeDampening(0).chat, 100u);
    EXPECT_EQ(ComputeDampening(200).chat, 0u);  // clamped
}

TEST(ChatMix, WheelLevelMapping) {
    EXPECT_EQ(BalanceFromWheelLevel(64), 50u);
    EXPECT_EQ(BalanceFromWheelLevel(0), 100u);  // full game
    EXPECT_EQ(BalanceFromWheelLevel(128), 0u);  // full chat
    EXPECT_EQ(BalanceFromWheelLevel(-5), 100u);
}

}  // namespace
}  // namespace priomix
