// SPDX-License-Identifier: GPL-3.0-only
//
// End-to-end tests of priomix-daemon in an isolated PipeWire/WirePlumber session: devices
// are null sinks/sources "plugged" by the test, a sine tone is played into the virtual
// sinks and the level arriving at the physical (null) sinks is measured.

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "pw_graph.hpp"
#include "test_session.hpp"

namespace priomix::test {
namespace {

using namespace std::chrono_literals;

constexpr const char* kService = "io.github.saijsaij.Priomix";
constexpr const char* kPath = "/io/github/saijsaij/Priomix";
constexpr const char* kInterface = "io.github.saijsaij.Priomix";

constexpr const char* kSinkA = "test-a";
constexpr const char* kSinkB = "test-b";
constexpr const char* kKeyA = "node:test-a";
constexpr const char* kKeyB = "node:test-b";

// Sine with amplitude 0.5 has an RMS of 0.5 / sqrt(2).
constexpr double kToneRms = 0.5 / 1.4142135623730951;
constexpr double kSilence = 0.01;

bool WaitFor(const std::function<bool()>& condition,
             std::chrono::milliseconds timeout = 5s) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        std::this_thread::sleep_for(50ms);
    }
    return condition();
}

class AudioPathTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string error;
        if (!session_.Start(error))
            GTEST_SKIP() << "No isolated audio session: " << error;
        ASSERT_TRUE(graph_.Connect()) << "Cannot connect to private PipeWire";
        ASSERT_TRUE(WaitFor([&] {
            return graph_.Metadata("default.audio.sink") || graph_.NodeId("Dummy-Driver");
        })) << "WirePlumber did not come up";

        // Start with one physical output so the loopbacks get a first target.
        ASSERT_TRUE(graph_.AddDevice(kSinkA, "Audio/Sink"));
        ASSERT_TRUE(session_.StartDaemon(error)) << error;
        ASSERT_TRUE(WaitFor([&] {
            return graph_.NodeId("priomix-game.output") &&
                   graph_.NodeId("priomix-chat.output");
        })) << "priomix virtual sinks did not appear";
        // The daemon writes the targets together with its initial volumes; once both
        // targets are visible, its start-up state is applied and cannot overwrite ours.
        ASSERT_TRUE(WaitFor([&] {
            return graph_.Metadata("target.object",
                                   *graph_.NodeId("priomix-game.output")) &&
                   graph_.Metadata("target.object",
                                   *graph_.NodeId("priomix-chat.output"));
        })) << "priomix did not set the loopback targets";

        connection_ = sdbus::createSessionBusConnection();
        proxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName{kService},
                                    sdbus::ObjectPath{kPath});
        ASSERT_TRUE(WaitFor([&] { return ActiveGame() == kKeyA; }))
            << "active game device: '" << ActiveGame() << "'";
    }

    void TearDown() override {
        if (HasFailure()) std::cerr << session_.Logs();
        proxy_.reset();
        connection_.reset();
    }

    // ---- D-Bus helpers ----
    std::string CallString(const char* method) {
        std::string result;
        proxy_->callMethod(method).onInterface(kInterface).storeResultsTo(result);
        return result;
    }
    uint32_t CallUint(const char* method) {
        uint32_t result = 0;
        proxy_->callMethod(method).onInterface(kInterface).storeResultsTo(result);
        return result;
    }
    template <typename... Args>
    void Call(const char* method, const Args&... args) {
        proxy_->callMethod(method).onInterface(kInterface).withArguments(args...);
    }
    std::string ActiveGame() { return CallString("GetActiveGameDevice"); }
    std::string ActiveChat() { return CallString("GetActiveChatDevice"); }

    // Plugs in a second sink; being new it goes to the top of both lists. The order is
    // then set explicitly so every test starts from a known state.
    void AddSinkB() {
        ASSERT_TRUE(graph_.AddDevice(kSinkB, "Audio/Sink"));
        ASSERT_TRUE(WaitFor([&] { return ActiveGame() == kKeyB; }));
    }

    // Meters both sinks and returns their RMS after routing had time to settle.
    std::pair<double, double> MeasureAB() {
        std::this_thread::sleep_for(300ms);
        double a = graph_.MeasureRms(kSinkA);
        double b = graph_.MeasureRms(kSinkB);
        return {a, b};
    }

    TestSession session_;
    PwGraph graph_;
    std::unique_ptr<sdbus::IConnection> connection_;
    std::unique_ptr<sdbus::IProxy> proxy_;
};

TEST_F(AudioPathTest, VirtualSinksAreDefaultAndTargetThePhysicalDevice) {
    ASSERT_TRUE(WaitFor([&] {
        auto sink = graph_.Metadata("default.audio.sink");
        return sink && sink->find("\"priomix-game\"") != std::string::npos;
    })) << "default.audio.sink = "
        << graph_.Metadata("default.audio.sink").value_or("-");

    auto output = graph_.NodeId("priomix-game.output");
    auto serial = graph_.NodeSerial(kSinkA);
    ASSERT_TRUE(output && serial);
    EXPECT_TRUE(WaitFor([&] {
        return graph_.Metadata("target.object", *output) == std::to_string(*serial);
    }));
}

TEST_F(AudioPathTest, AudioReachesTheActiveDeviceOnly) {
    AddSinkB();
    ASSERT_TRUE(graph_.StartMeter(kSinkA));
    ASSERT_TRUE(graph_.StartMeter(kSinkB));
    ASSERT_TRUE(graph_.StartTone("priomix-game"));

    auto [a, b] = MeasureAB();
    EXPECT_LT(a, kSilence);
    EXPECT_NEAR(b, kToneRms, 0.03);

    // Reordering switches the output live.
    Call("SetGameSinkPriority", std::vector<std::string>{kKeyA, kKeyB});
    ASSERT_TRUE(WaitFor([&] { return ActiveGame() == kKeyA; }));
    std::tie(a, b) = MeasureAB();
    EXPECT_NEAR(a, kToneRms, 0.03);
    EXPECT_LT(b, kSilence);
}

TEST_F(AudioPathTest, UnpluggingFallsBackToNextDevice) {
    AddSinkB();
    ASSERT_TRUE(graph_.StartMeter(kSinkA));
    ASSERT_TRUE(graph_.StartTone("priomix-game"));

    graph_.RemoveDevice(kSinkB);
    ASSERT_TRUE(WaitFor([&] { return ActiveGame() == kKeyA; }));
    auto [a, b] = MeasureAB();
    EXPECT_NEAR(a, kToneRms, 0.03);

    // Plugging it back in makes it win again: it is first in the list now.
    ASSERT_TRUE(graph_.AddDevice(kSinkB, "Audio/Sink"));
    EXPECT_TRUE(WaitFor([&] { return ActiveGame() == kKeyB; }));
}

TEST_F(AudioPathTest, GameVolumeScalesTheSignalCubically) {
    ASSERT_TRUE(graph_.StartMeter(kSinkA));
    ASSERT_TRUE(graph_.StartTone("priomix-game"));
    std::this_thread::sleep_for(300ms);
    double full = graph_.MeasureRms(kSinkA);
    ASSERT_NEAR(full, kToneRms, 0.03);

    Call("SetGameVolume", uint32_t{50});
    std::this_thread::sleep_for(300ms);
    double half = graph_.MeasureRms(kSinkA);
    // 50 % on the cubic scale is a linear gain of 0.125.
    EXPECT_NEAR(half / full, 0.125, 0.02);
    EXPECT_EQ(CallUint("GetGameVolume"), 50u);
}

TEST_F(AudioPathTest, ExternalVolumeChangeIsAdopted) {
    // First make sure the daemon's start-up volume has reached the node, otherwise it may
    // arrive after our change and (correctly) win: at start the config is authoritative.
    Call("SetGameVolume", uint32_t{90});
    ASSERT_TRUE(WaitFor([&] {
        auto v = graph_.NodeVolume("priomix-game");
        return v && std::fabs(*v - 0.729f) < 0.001f;  // 0.9^3
    }));

    // What the Plasma tray does: set channelVolumes on the virtual sink. This happens
    // right after the daemon's own write, i.e. inside its echo window.
    ASSERT_TRUE(graph_.SetNodeVolume("priomix-game", 0.343f));  // 0.7^3
    EXPECT_TRUE(WaitFor([&] { return CallUint("GetGameVolume") == 70u; }))
        << "game volume: " << CallUint("GetGameVolume");
    // The daemon adopts it instead of fighting back.
    std::this_thread::sleep_for(500ms);
    auto v = graph_.NodeVolume("priomix-game");
    ASSERT_TRUE(v);
    EXPECT_NEAR(*v, 0.343f, 0.001f);
}

TEST_F(AudioPathTest, BalanceDampensOnlyTheOtherRole) {
    AddSinkB();
    // Game -> A, chat -> B.
    Call("SetSyncPriorityLists", false);
    Call("SetGameSinkPriority", std::vector<std::string>{kKeyA, kKeyB});
    Call("SetChatSinkPriority", std::vector<std::string>{kKeyB, kKeyA});
    ASSERT_TRUE(WaitFor([&] { return ActiveGame() == kKeyA && ActiveChat() == kKeyB; }));

    ASSERT_TRUE(graph_.StartMeter(kSinkA));
    ASSERT_TRUE(graph_.StartMeter(kSinkB));
    ASSERT_TRUE(graph_.StartTone("priomix-game"));
    ASSERT_TRUE(graph_.StartTone("priomix-chat"));

    auto [game, chat] = MeasureAB();
    ASSERT_NEAR(game, kToneRms, 0.03);
    ASSERT_NEAR(chat, kToneRms, 0.03);

    // 75 = more game: chat dampened to 50 % (cubic: 0.125), game untouched.
    Call("SetChatMixBalance", uint32_t{75});
    auto [game75, chat75] = MeasureAB();
    EXPECT_NEAR(game75 / game, 1.0, 0.05);
    EXPECT_NEAR(chat75 / chat, 0.125, 0.02);

    // 25 = more chat: the other way round.
    Call("SetChatMixBalance", uint32_t{25});
    auto [game25, chat25] = MeasureAB();
    EXPECT_NEAR(game25 / game, 0.125, 0.02);
    EXPECT_NEAR(chat25 / chat, 1.0, 0.05);

    Call("AdjustChatMixBalance", int32_t{25});
    EXPECT_EQ(CallUint("GetChatMixBalance"), 50u);
    auto [game50, chat50] = MeasureAB();
    EXPECT_NEAR(game50 / game, 1.0, 0.05);
    EXPECT_NEAR(chat50 / chat, 1.0, 0.05);
}

TEST_F(AudioPathTest, IgnoringTheOnlyDeviceSilencesTheOutput) {
    ASSERT_TRUE(graph_.StartMeter(kSinkA));
    ASSERT_TRUE(graph_.StartTone("priomix-game"));
    std::this_thread::sleep_for(300ms);
    ASSERT_NEAR(graph_.MeasureRms(kSinkA), kToneRms, 0.03);

    Call("SetSinkIgnored", std::string(kKeyA), true);
    ASSERT_TRUE(WaitFor([&] { return ActiveGame().empty(); }));
    std::this_thread::sleep_for(300ms);
    EXPECT_LT(graph_.MeasureRms(kSinkA), kSilence);

    Call("SetSinkIgnored", std::string(kKeyA), false);
    ASSERT_TRUE(WaitFor([&] { return ActiveGame() == kKeyA; }));
    std::this_thread::sleep_for(300ms);
    EXPECT_NEAR(graph_.MeasureRms(kSinkA), kToneRms, 0.03);
}

TEST_F(AudioPathTest, MicrophonePriorityDrivesDefaultSource) {
    auto default_source_is = [&](const std::string& name) {
        auto value = graph_.Metadata("default.configured.audio.source");
        return value && value->find("\"" + name + "\"") != std::string::npos;
    };
    ASSERT_TRUE(graph_.AddDevice("mic-a", "Audio/Source"));
    ASSERT_TRUE(WaitFor([&] { return default_source_is("mic-a"); }));
    ASSERT_TRUE(graph_.AddDevice("mic-b", "Audio/Source"));
    ASSERT_TRUE(WaitFor([&] { return default_source_is("mic-b"); }));

    Call("SetSourcePriority", std::vector<std::string>{"node:mic-a", "node:mic-b"});
    EXPECT_TRUE(WaitFor([&] { return default_source_is("mic-a"); }));
    EXPECT_EQ(CallString("GetActiveSource"), "node:mic-a");
}

TEST_F(AudioPathTest, ChatPriorityIsRejectedWhileSynced) {
    try {
        Call("SetChatSinkPriority", std::vector<std::string>{kKeyA});
        FAIL() << "expected an error";
    } catch (const sdbus::Error& e) {
        EXPECT_EQ(e.getName(), "io.github.saijsaij.Priomix.Error.SyncEnabled");
    }
}

TEST_F(AudioPathTest, SettingsSurviveADaemonRestart) {
    Call("SetGameVolume", uint32_t{40});
    Call("SetChatMixBalance", uint32_t{60});
    session_.StopDaemon();  // flushes the config on SIGTERM
    ASSERT_TRUE(WaitFor([&] { return !graph_.NodeId("priomix-game"); }));

    std::string error;
    ASSERT_TRUE(session_.StartDaemon(error)) << error;
    ASSERT_TRUE(WaitFor([&] {
        try {
            return CallUint("GetGameVolume") == 40u;
        } catch (const sdbus::Error&) {
            return false;  // not registered yet
        }
    }));
    EXPECT_EQ(CallUint("GetChatMixBalance"), 60u);
}

}  // namespace
}  // namespace priomix::test
