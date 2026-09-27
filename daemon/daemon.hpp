// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Central state of priomix-daemon: wires PipeWire, headset monitor, UPower, config and
// the D-Bus service together and runs the priority decision ("evaluation"). Single
// threaded, everything is driven by the sd-event loop.

#include <systemd/sd-event.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "chatmix_engine.hpp"
#include "config_store.hpp"
#include "device_cache.hpp"
#include "device_id.hpp"
#include "event_timer.hpp"

namespace priomix {

class DbusService;
class HeadsetMonitor;
class PipeWireRegistry;
class PowerState;

struct DeviceInfo {
    std::string key;
    std::string display_name;
    bool available = false;
    bool ignored = false;

    bool operator==(const DeviceInfo&) const = default;
};

// Result of a D-Bus facing operation; error is a D-Bus error name.
struct Status {
    std::string error;
    std::string message;
    bool Ok() const { return error.empty(); }
    static Status Success() { return {}; }
};

class Daemon {
public:
    explicit Daemon(sd_event* event);
    ~Daemon();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    bool Start();
    // Persists pending changes and removes the virtual sinks.
    void Shutdown();

    // ---- API exposed on D-Bus (see common/io.github.saijsaij.Priomix.xml) ----
    std::vector<DeviceInfo> ListSinks() const;
    std::vector<DeviceInfo> ListSources() const;
    Status SetSinkIgnored(const std::string& key, bool ignored);
    Status SetSourceIgnored(const std::string& key, bool ignored);

    std::vector<std::string> GameSinkPriority() const;
    Status SetGameSinkPriority(const std::vector<std::string>& keys);
    std::vector<std::string> ChatSinkPriority() const;
    Status SetChatSinkPriority(const std::vector<std::string>& keys);
    std::vector<std::string> SourcePriority() const;
    Status SetSourcePriority(const std::vector<std::string>& keys);
    bool SyncPriorityLists() const { return config_.sync_priority_lists; }
    Status SetSyncPriorityLists(bool enabled);

    std::string ActiveGameDevice() const { return active_game_.value_or(""); }
    std::string ActiveChatDevice() const { return active_chat_.value_or(""); }
    std::string ActiveSource() const { return active_source_.value_or(""); }

    std::tuple<uint32_t, uint32_t> PollIntervals() const;
    Status SetPollIntervals(uint32_t ac_ms, uint32_t battery_ms);
    void TriggerReevaluation();

    uint32_t GameVolume() const { return config_.game_volume; }
    Status SetGameVolume(uint32_t volume);
    uint32_t ChatVolume() const { return config_.chat_volume; }
    Status SetChatVolume(uint32_t volume);

    uint32_t ChatMixBalance() const { return balance_; }
    Status SetChatMixBalance(uint32_t balance);
    Status AdjustChatMixBalance(int32_t delta);
    bool HardwareChatMixActive() const { return hardware_chatmix_active_; }

private:
    struct PresentDevice {
        std::string key;
        DeviceIdentity identity;
        uint32_t node_id = 0;
        uint64_t serial = 0;
        std::string node_name;
    };
    using PresentMap = std::map<std::string, PresentDevice>;

    void OnPipeWireReady();
    void OnGraphChanged();
    void OnVolumeChanged(uint32_t node_id);
    void OnExternalVolumeChange(ChatMixRole role, uint32_t volume);
    void OnPowerChanged(bool on_battery);
    void ApplyPollInterval();

    // Full decision run: key resolution, winner selection, routing, signals.
    void Evaluate(bool force = false);
    void CollectPresentDevices(PresentMap& sinks, PresentMap& sources);
    bool IsAvailable(const PresentMap& present, const std::string& key) const;
    void UpdateHardwareChatMix();
    void EnsureDefaultSink();
    void SetDefaultSource(const PresentDevice& device);
    void ApplyBalance(uint32_t balance, bool persist);
    void EmitListChanges();

    std::vector<DeviceInfo> BuildDeviceList(const std::vector<const PriorityList*>& lists,
                                            const std::vector<std::string>& ignored,
                                            const PresentMap& present) const;
    std::optional<uint64_t> SerialOf(const PresentMap& present,
                                     const std::optional<std::string>& key) const;
    void ScheduleSave();
    void SaveNow();

    sd_event* event_;
    ConfigStore config_store_;
    DeviceCache device_cache_;
    Config config_;

    std::unique_ptr<PipeWireRegistry> registry_;
    std::unique_ptr<ChatMixEngine> chatmix_;
    std::unique_ptr<HeadsetMonitor> headsets_;
    std::unique_ptr<PowerState> power_;
    std::unique_ptr<DbusService> dbus_;

    EventTimer evaluate_timer_;
    EventTimer save_timer_;

    PresentMap present_sinks_;
    PresentMap present_sources_;
    std::optional<std::string> active_game_;
    std::optional<std::string> active_chat_;
    std::optional<std::string> active_source_;
    bool first_evaluation_ = true;
    bool default_sink_set_ = false;

    bool hardware_chatmix_active_ = false;
    uint32_t balance_ = 50;

    // Last state announced on D-Bus, to emit change signals only on real changes.
    std::vector<DeviceInfo> announced_sinks_;
    std::vector<DeviceInfo> announced_sources_;
    std::vector<std::string> announced_game_;
    std::vector<std::string> announced_chat_;
    std::vector<std::string> announced_source_prio_;
};

}  // namespace priomix
