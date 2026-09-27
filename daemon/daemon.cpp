// SPDX-License-Identifier: GPL-3.0-only
#include "daemon.hpp"

#include <algorithm>
#include <set>

#include "dbus_service.hpp"
#include "headset_monitor.hpp"
#include "logging.hpp"
#include "pipewire_registry.hpp"
#include "power_state.hpp"
#include "priority_engine.hpp"

namespace priomix {

namespace {

constexpr uint64_t kEvaluateDebounceMs = 100;
constexpr uint64_t kSaveDebounceMs = 500;
constexpr uint32_t kMinPollMs = 500;
constexpr uint32_t kMaxPollMs = 3600 * 1000;
constexpr uint32_t kMaxVolume = 150;

constexpr const char* kErrorInvalidArgs = "org.freedesktop.DBus.Error.InvalidArgs";
constexpr const char* kErrorSyncEnabled = "io.github.saijsaij.Priomix.Error.SyncEnabled";
constexpr const char* kErrorHardwareChatMix =
    "io.github.saijsaij.Priomix.Error.HardwareChatMix";

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

bool SetMembership(std::vector<std::string>& v, const std::string& s, bool member) {
    bool present = Contains(v, s);
    if (member == present) return false;
    if (member) {
        v.push_back(s);
    } else {
        v.erase(std::remove(v.begin(), v.end(), s), v.end());
    }
    return true;
}

// JSON string literal for metadata values like {"name":"..."}.
std::string JsonQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

}  // namespace

Daemon::Daemon(sd_event* event)
    : event_(event),
      config_store_(DefaultConfigDir() / "config.json"),
      device_cache_(DefaultConfigDir() / "known_devices.json"),
      evaluate_timer_(event, [this] { Evaluate(); }),
      save_timer_(event, [this] { SaveNow(); }) {}

Daemon::~Daemon() = default;

bool Daemon::Start() {
    if (config_store_.Load(config_)) {
        PRIOMIX_LOG_INFO("Loaded %s", config_store_.Path().c_str());
    } else {
        PRIOMIX_LOG_INFO("No usable %s, starting with defaults",
                         config_store_.Path().c_str());
    }
    device_cache_.Load();
    if (config_.sync_priority_lists)
        config_.chat_sink_priority = config_.game_sink_priority;
    balance_ = config_.chatmix_balance;

    // D-Bus first: a second instance fails here before touching PipeWire.
    try {
        dbus_ = std::make_unique<DbusService>(*this, event_);
    } catch (const std::exception& e) {
        PRIOMIX_LOG_ERROR("Cannot register D-Bus service: %s", e.what());
        return false;
    }

    registry_ = std::make_unique<PipeWireRegistry>(PipeWireRegistry::Callbacks{
        .ready = [this] { OnPipeWireReady(); },
        .graph_changed = [this] { OnGraphChanged(); },
        .volume_changed = [this](uint32_t id) { OnVolumeChanged(id); },
        .fatal_error =
            [this] {
                PRIOMIX_LOG_ERROR("Lost connection to PipeWire, exiting");
                sd_event_exit(event_, 1);
            },
    });
    if (!registry_->Connect(event_)) return false;

    chatmix_ = std::make_unique<ChatMixEngine>(
        event_, *registry_,
        [this](ChatMixRole role, uint32_t v) { OnExternalVolumeChange(role, v); });
    chatmix_->SetVolume(ChatMixRole::Game, config_.game_volume);
    chatmix_->SetVolume(ChatMixRole::Chat, config_.chat_volume);
    chatmix_->SetBalance(balance_);

    headsets_ = std::make_unique<HeadsetMonitor>(event_, [this] {
        // Headset state changes are rare and important: evaluate right away.
        evaluate_timer_.Start(0);
    });

    power_ = std::make_unique<PowerState>(
        [this](bool on_battery) { OnPowerChanged(on_battery); });
    power_->Start(event_);
    ApplyPollInterval();
    return true;
}

void Daemon::Shutdown() {
    SaveNow();
    device_cache_.SaveIfDirty();
    if (chatmix_) chatmix_->Unload();
}

// ---------------------------------------------------------------------------------------
// Event handlers

void Daemon::OnPipeWireReady() {
    // Initial headset state before the first decision, so a switched-off headset never
    // becomes active for a moment.
    headsets_->PollNow();
    Evaluate();
    chatmix_->Load(SerialOf(present_sinks_, active_game_),
                   SerialOf(present_sinks_, active_chat_));
    PRIOMIX_LOG_INFO("priomix-daemon started, entering main loop");
}

void Daemon::OnGraphChanged() {
    chatmix_->Sync();
    EnsureDefaultSink();
    evaluate_timer_.StartIfIdle(kEvaluateDebounceMs);
}

void Daemon::OnVolumeChanged(uint32_t node_id) {
    chatmix_->OnVolumeChanged(node_id);
}

void Daemon::OnExternalVolumeChange(ChatMixRole role, uint32_t volume) {
    PRIOMIX_LOG_DEBUG("%s volume changed externally to %u%%", RoleName(role), volume);
    if (role == ChatMixRole::Game) {
        config_.game_volume = volume;
        dbus_->emitGameVolumeChanged(volume);
    } else {
        config_.chat_volume = volume;
        dbus_->emitChatVolumeChanged(volume);
    }
    ScheduleSave();
}

void Daemon::OnPowerChanged(bool) {
    ApplyPollInterval();
}

void Daemon::ApplyPollInterval() {
    bool on_battery = power_ && power_->OnBattery();
    headsets_->SetInterval(on_battery ? config_.headset_poll_interval_battery_ms
                                      : config_.headset_poll_interval_ac_ms);
}

// ---------------------------------------------------------------------------------------
// Evaluation

void Daemon::CollectPresentDevices(PresentMap& sinks, PresentMap& sources) {
    std::vector<KeyRequest> sink_requests;
    std::vector<KeyRequest> source_requests;
    std::vector<PresentDevice> sink_devices;
    std::vector<PresentDevice> source_devices;

    static const Properties kNoProps;
    for (const auto& [id, node] : registry_->Nodes()) {
        if (!node.info_received) continue;
        const std::string name = node.Name();
        if (name.rfind(PipeWireRegistry::kOwnNodePrefix, 0) == 0) continue;
        bool is_sink = node.media_class == "Audio/Sink";
        bool is_source = node.media_class == "Audio/Source";
        if (!is_sink && !is_source) continue;

        const PwDevice* device = registry_->FindDevice(node.device_id);
        PresentDevice present;
        present.identity =
            DeriveDeviceIdentity(node.props, device ? device->props : kNoProps);
        present.node_id = node.id;
        present.serial = node.serial;
        present.node_name = name;

        KeyRequest request{present.identity.id, present.identity.bus_path, name, {}};
        (is_sink ? sink_requests : source_requests).push_back(request);
        (is_sink ? sink_devices : source_devices).push_back(std::move(present));
    }

    bool modified = ResolveKeys(
        sink_requests, {&config_.game_sink_priority, &config_.chat_sink_priority},
        {&config_.ignored_sinks});
    modified |= ResolveKeys(source_requests, {&config_.source_priority},
                            {&config_.ignored_sources});
    if (config_.sync_priority_lists &&
        config_.chat_sink_priority != config_.game_sink_priority) {
        config_.chat_sink_priority = config_.game_sink_priority;
        modified = true;
    }
    if (modified) ScheduleSave();

    for (size_t i = 0; i < sink_devices.size(); ++i) {
        sink_devices[i].key = sink_requests[i].key;
        device_cache_.Update(sink_devices[i].key, sink_devices[i].identity.display_name);
        sinks[sink_devices[i].key] = std::move(sink_devices[i]);
    }
    for (size_t i = 0; i < source_devices.size(); ++i) {
        source_devices[i].key = source_requests[i].key;
        device_cache_.Update(source_devices[i].key,
                             source_devices[i].identity.display_name);
        sources[source_devices[i].key] = std::move(source_devices[i]);
    }
}

bool Daemon::IsAvailable(const PresentMap& present, const std::string& key) const {
    auto it = present.find(key);
    if (it == present.end()) return false;
    const std::string& usb_id = it->second.identity.usb_id;
    if (usb_id.empty()) return true;
    // Dongle present is not enough for supported wireless headsets.
    const HeadsetState* headset = headsets_->Find(usb_id);
    return headset == nullptr || headset->powered;
}

std::optional<uint64_t> Daemon::SerialOf(const PresentMap& present,
                                         const std::optional<std::string>& key) const {
    if (!key) return std::nullopt;
    auto it = present.find(*key);
    if (it == present.end()) return std::nullopt;
    return it->second.serial;
}

void Daemon::Evaluate(bool force) {
    if (!registry_ || !registry_->IsReady()) return;
    evaluate_timer_.Stop();

    PresentMap sinks;
    PresentMap sources;
    CollectPresentDevices(sinks, sources);
    present_sinks_ = std::move(sinks);
    present_sources_ = std::move(sources);

    auto sink_available = [this](const std::string& k) {
        return IsAvailable(present_sinks_, k);
    };
    auto source_available = [this](const std::string& k) {
        return IsAvailable(present_sources_, k);
    };
    auto game =
        PickWinner(config_.game_sink_priority, config_.ignored_sinks, sink_available);
    auto chat =
        PickWinner(config_.chat_sink_priority, config_.ignored_sinks, sink_available);
    auto source =
        PickWinner(config_.source_priority, config_.ignored_sources, source_available);

    // Output routing: never touch default.audio.sink, only retarget the loopbacks. The
    // serial is re-sent every time; ChatMixEngine skips unchanged targets itself.
    chatmix_->Retarget(ChatMixRole::Game, SerialOf(present_sinks_, game));
    chatmix_->Retarget(ChatMixRole::Chat, SerialOf(present_sinks_, chat));

    if (game != active_game_ || first_evaluation_) {
        PRIOMIX_LOG_INFO("Active game device: %s", game.value_or("(none)").c_str());
        active_game_ = game;
        dbus_->emitActiveGameDeviceChanged(ActiveGameDevice());
    }
    if (chat != active_chat_ || first_evaluation_) {
        PRIOMIX_LOG_INFO("Active chat device: %s", chat.value_or("(none)").c_str());
        active_chat_ = chat;
        dbus_->emitActiveChatDeviceChanged(ActiveChatDevice());
    }
    if (source != active_source_ || first_evaluation_ || force) {
        if (source != active_source_ || first_evaluation_) {
            PRIOMIX_LOG_INFO("Active source: %s", source.value_or("(none)").c_str());
        }
        active_source_ = source;
        if (source) SetDefaultSource(present_sources_.at(*source));
        dbus_->emitActiveSourceChanged(ActiveSource());
    }
    first_evaluation_ = false;

    UpdateHardwareChatMix();
    EnsureDefaultSink();
    EmitListChanges();
    device_cache_.SaveIfDirty();
}

void Daemon::UpdateHardwareChatMix() {
    std::string watch;
    std::optional<int> level;
    if (auto it = present_sinks_.find(ActiveGameDevice()); it != present_sinks_.end()) {
        const HeadsetState* headset = headsets_->Find(it->second.identity.usb_id);
        if (headset != nullptr && headset->powered && headset->supports_chatmix) {
            watch = headset->usb_id;
            level = headset->chatmix_level;
        }
    }
    headsets_->WatchChatMix(watch);
    // WatchChatMix may have read the wheel right now.
    if (!watch.empty()) level = headsets_->Find(watch)->chatmix_level;

    bool active = level.has_value();
    if (active != hardware_chatmix_active_) {
        hardware_chatmix_active_ = active;
        PRIOMIX_LOG_INFO("Hardware ChatMix wheel %s", active ? "active" : "inactive");
        dbus_->emitHardwareChatMixActiveChanged(active);
        // Without a wheel the last software balance applies again.
        if (!active) ApplyBalance(config_.chatmix_balance, false);
    }
    if (active) ApplyBalance(BalanceFromWheelLevel(*level), false);
}

void Daemon::ApplyBalance(uint32_t balance, bool persist) {
    balance = std::min<uint32_t>(balance, 100);
    if (persist && balance != config_.chatmix_balance) {
        config_.chatmix_balance = balance;
        ScheduleSave();
    }
    if (balance == balance_) return;
    balance_ = balance;
    chatmix_->SetBalance(balance);
    PRIOMIX_LOG_DEBUG("ChatMix balance: %u", balance);
    dbus_->emitChatMixBalanceChanged(balance);
}

void Daemon::EnsureDefaultSink() {
    const PwNode* sink =
        registry_->FindNodeByName(ChatMixEngine::SinkName(ChatMixRole::Game));
    if (sink == nullptr) {
        default_sink_set_ = false;  // set again once the loopback (re)appears
        return;
    }
    if (default_sink_set_ || !registry_->HasDefaultMetadata()) return;
    // default.configured.* is what WirePlumber persists and derives default.audio.sink
    // from (the same key "wpctl set-default" writes).
    std::string value = "{\"name\":" + JsonQuote(sink->Name()) + "}";
    if (registry_->SetMetadata(0, "default.configured.audio.sink", "Spa:String:JSON",
                               value.c_str())) {
        default_sink_set_ = true;
        PRIOMIX_LOG_INFO("Default sink set to %s", sink->Name().c_str());
    }
}

void Daemon::SetDefaultSource(const PresentDevice& device) {
    std::string value = "{\"name\":" + JsonQuote(device.node_name) + "}";
    registry_->SetMetadata(0, "default.configured.audio.source", "Spa:String:JSON",
                           value.c_str());
    PRIOMIX_LOG_DEBUG("Default source set to %s", device.node_name.c_str());
}

std::vector<DeviceInfo> Daemon::BuildDeviceList(
    const std::vector<const PriorityList*>& lists,
    const std::vector<std::string>& ignored, const PresentMap& present) const {
    std::vector<DeviceInfo> devices;
    std::set<std::string> seen;
    for (const auto* list : lists) {
        for (const auto& entry : *list) {
            std::string key = entry.Key();
            if (!seen.insert(key).second) continue;
            DeviceInfo info;
            info.key = key;
            info.display_name = device_cache_.NameOf(key);
            if (auto it = present.find(key);
                it != present.end() && info.display_name.empty()) {
                info.display_name = it->second.identity.display_name;
            }
            info.available = IsAvailable(present, key);
            info.ignored = Contains(ignored, key);
            devices.push_back(std::move(info));
        }
    }
    return devices;
}

void Daemon::EmitListChanges() {
    auto sinks = ListSinks();
    if (sinks != announced_sinks_) {
        announced_sinks_ = std::move(sinks);
        dbus_->emitSinksChanged();
    }
    auto sources = ListSources();
    if (sources != announced_sources_) {
        announced_sources_ = std::move(sources);
        dbus_->emitSourcesChanged();
    }
    auto game = GameSinkPriority();
    if (game != announced_game_) {
        announced_game_ = std::move(game);
        dbus_->emitGameSinkPriorityChanged();
    }
    auto chat = ChatSinkPriority();
    if (chat != announced_chat_) {
        announced_chat_ = std::move(chat);
        dbus_->emitChatSinkPriorityChanged();
    }
    auto source = SourcePriority();
    if (source != announced_source_prio_) {
        announced_source_prio_ = std::move(source);
        dbus_->emitSourcePriorityChanged();
    }
}

// ---------------------------------------------------------------------------------------
// Persistence

void Daemon::ScheduleSave() {
    save_timer_.StartIfIdle(kSaveDebounceMs);
}

void Daemon::SaveNow() {
    save_timer_.Stop();
    if (config_store_.Save(config_)) {
        PRIOMIX_LOG_DEBUG("Saved %s", config_store_.Path().c_str());
    }
}

// ---------------------------------------------------------------------------------------
// D-Bus API

std::vector<DeviceInfo> Daemon::ListSinks() const {
    return BuildDeviceList({&config_.game_sink_priority, &config_.chat_sink_priority},
                           config_.ignored_sinks, present_sinks_);
}

std::vector<DeviceInfo> Daemon::ListSources() const {
    return BuildDeviceList({&config_.source_priority}, config_.ignored_sources,
                           present_sources_);
}

Status Daemon::SetSinkIgnored(const std::string& key, bool ignored) {
    if (key.empty()) return {kErrorInvalidArgs, "Empty device key"};
    if (SetMembership(config_.ignored_sinks, key, ignored)) {
        PRIOMIX_LOG_INFO("Sink %s %s", key.c_str(),
                         ignored ? "ignored" : "no longer ignored");
        ScheduleSave();
        Evaluate();
    }
    return Status::Success();
}

Status Daemon::SetSourceIgnored(const std::string& key, bool ignored) {
    if (key.empty()) return {kErrorInvalidArgs, "Empty device key"};
    if (SetMembership(config_.ignored_sources, key, ignored)) {
        PRIOMIX_LOG_INFO("Source %s %s", key.c_str(),
                         ignored ? "ignored" : "no longer ignored");
        ScheduleSave();
        Evaluate();
    }
    return Status::Success();
}

std::vector<std::string> Daemon::GameSinkPriority() const {
    return Keys(config_.game_sink_priority);
}

Status Daemon::SetGameSinkPriority(const std::vector<std::string>& keys) {
    config_.game_sink_priority = Reorder(config_.game_sink_priority, keys);
    if (config_.sync_priority_lists)
        config_.chat_sink_priority = config_.game_sink_priority;
    ScheduleSave();
    Evaluate();
    return Status::Success();
}

std::vector<std::string> Daemon::ChatSinkPriority() const {
    return Keys(config_.chat_sink_priority);
}

Status Daemon::SetChatSinkPriority(const std::vector<std::string>& keys) {
    if (config_.sync_priority_lists) {
        return {kErrorSyncEnabled,
                "Chat priority follows the game priority while synced"};
    }
    config_.chat_sink_priority = Reorder(config_.chat_sink_priority, keys);
    ScheduleSave();
    Evaluate();
    return Status::Success();
}

std::vector<std::string> Daemon::SourcePriority() const {
    return Keys(config_.source_priority);
}

Status Daemon::SetSourcePriority(const std::vector<std::string>& keys) {
    config_.source_priority = Reorder(config_.source_priority, keys);
    ScheduleSave();
    Evaluate();
    return Status::Success();
}

Status Daemon::SetSyncPriorityLists(bool enabled) {
    if (enabled == config_.sync_priority_lists) return Status::Success();
    config_.sync_priority_lists = enabled;
    // Enabling copies game -> chat; disabling starts from a copy of the game list, which
    // the chat list already is while synced.
    if (enabled) config_.chat_sink_priority = config_.game_sink_priority;
    dbus_->emitSyncPriorityListsChanged(enabled);
    ScheduleSave();
    Evaluate();
    return Status::Success();
}

std::tuple<uint32_t, uint32_t> Daemon::PollIntervals() const {
    return {config_.headset_poll_interval_ac_ms,
            config_.headset_poll_interval_battery_ms};
}

Status Daemon::SetPollIntervals(uint32_t ac_ms, uint32_t battery_ms) {
    if (ac_ms < kMinPollMs || battery_ms < kMinPollMs || ac_ms > kMaxPollMs ||
        battery_ms > kMaxPollMs) {
        return {kErrorInvalidArgs, "Poll intervals must be between 500 ms and 1 h"};
    }
    config_.headset_poll_interval_ac_ms = ac_ms;
    config_.headset_poll_interval_battery_ms = battery_ms;
    ApplyPollInterval();
    ScheduleSave();
    return Status::Success();
}

void Daemon::TriggerReevaluation() {
    PRIOMIX_LOG_INFO("Re-evaluation requested");
    headsets_->PollNow();
    Evaluate(true);
}

Status Daemon::SetGameVolume(uint32_t volume) {
    volume = std::min(volume, kMaxVolume);
    chatmix_->SetVolume(ChatMixRole::Game, volume);
    if (volume != config_.game_volume) {
        config_.game_volume = volume;
        dbus_->emitGameVolumeChanged(volume);
        ScheduleSave();
    }
    return Status::Success();
}

Status Daemon::SetChatVolume(uint32_t volume) {
    volume = std::min(volume, kMaxVolume);
    chatmix_->SetVolume(ChatMixRole::Chat, volume);
    if (volume != config_.chat_volume) {
        config_.chat_volume = volume;
        dbus_->emitChatVolumeChanged(volume);
        ScheduleSave();
    }
    return Status::Success();
}

Status Daemon::SetChatMixBalance(uint32_t balance) {
    if (hardware_chatmix_active_) {
        return {kErrorHardwareChatMix,
                "The ChatMix balance is controlled by the headset wheel"};
    }
    ApplyBalance(std::min<uint32_t>(balance, 100), true);
    return Status::Success();
}

Status Daemon::AdjustChatMixBalance(int32_t delta) {
    if (hardware_chatmix_active_) {
        return {kErrorHardwareChatMix,
                "The ChatMix balance is controlled by the headset wheel"};
    }
    int64_t balance = std::clamp<int64_t>(static_cast<int64_t>(balance_) + delta, 0, 100);
    ApplyBalance(static_cast<uint32_t>(balance), true);
    return Status::Success();
}

}  // namespace priomix
