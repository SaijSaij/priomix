// SPDX-License-Identifier: GPL-3.0-only
#include "config_store.hpp"

#include <filesystem>
#include <gtest/gtest.h>

namespace priomix {
namespace {

class ConfigStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("priomix-test-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir_);
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path dir_;
};

TEST_F(ConfigStoreTest, MissingFileKeepsDefaults) {
    ConfigStore store(dir_ / "config.json");
    Config config;
    EXPECT_FALSE(store.Load(config));
    EXPECT_TRUE(config.sync_priority_lists);
    EXPECT_EQ(config.headset_poll_interval_ac_ms, 5000u);
    EXPECT_EQ(config.headset_poll_interval_battery_ms, 30000u);
}

TEST_F(ConfigStoreTest, SaveAndLoadRoundTrip) {
    ConfigStore store(dir_ / "sub" / "config.json");
    Config config;
    config.game_sink_priority = {{"usb:1038:12ad", "usb-0000:00:14.0-2"},
                                 {"alsa:hdmi0", ""}};
    config.chat_sink_priority = {{"alsa:hdmi0", ""}};
    config.sync_priority_lists = false;
    config.source_priority = {{"alsa:internal-mic", ""}};
    config.ignored_sinks = {"alsa:hdmi1"};
    config.game_volume = 42;
    config.chatmix_balance = 70;
    config.headset_poll_interval_battery_ms = 60000;
    ASSERT_TRUE(store.Save(config));

    Config loaded;
    ASSERT_TRUE(store.Load(loaded));
    EXPECT_EQ(loaded.game_sink_priority, config.game_sink_priority);
    EXPECT_EQ(loaded.chat_sink_priority, config.chat_sink_priority);
    EXPECT_FALSE(loaded.sync_priority_lists);
    EXPECT_EQ(loaded.source_priority, config.source_priority);
    EXPECT_EQ(loaded.ignored_sinks, config.ignored_sinks);
    EXPECT_EQ(loaded.game_volume, 42u);
    EXPECT_EQ(loaded.chatmix_balance, 70u);
    EXPECT_EQ(loaded.headset_poll_interval_battery_ms, 60000u);
}

TEST_F(ConfigStoreTest, ToleratesHandEditedValues) {
    std::string text = R"({"game_sink_priority": ["alsa:a", {"id": "alsa:b"}, {"id": ""}],
                           "chatmix_balance": 500, "headset_poll_interval_ac_ms": 1})";
    ASSERT_TRUE(WriteFileAtomically(dir_ / "config.json", text));
    ConfigStore store(dir_ / "config.json");
    Config config;
    ASSERT_TRUE(store.Load(config));
    ASSERT_EQ(config.game_sink_priority.size(), 2u);
    EXPECT_EQ(config.game_sink_priority[0].id, "alsa:a");
    EXPECT_EQ(config.chatmix_balance, 100u);
    EXPECT_EQ(config.headset_poll_interval_ac_ms, 500u);
}

TEST(PriorityEntry, KeyRoundTrip) {
    EXPECT_EQ((PriorityEntry{"usb:1:2", ""}).Key(), "usb:1:2");
    EXPECT_EQ((PriorityEntry{"usb:1:2", "p"}).Key(), "usb:1:2@p");
    EXPECT_EQ(PriorityEntry::FromKey("usb:1:2@p"), (PriorityEntry{"usb:1:2", "p"}));
    EXPECT_EQ(PriorityEntry::FromKey("bt:AA:BB"), (PriorityEntry{"bt:AA:BB", ""}));
}

}  // namespace
}  // namespace priomix
