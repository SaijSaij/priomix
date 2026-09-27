// SPDX-License-Identifier: GPL-3.0-only
#include "chatmix_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "logging.hpp"
#include "pipewire_registry.hpp"
#include "priority_engine.hpp"

namespace priomix {

namespace {

// Parameter echoes of our own volume writes arrive asynchronously. Within this window
// after a write, differing reports are treated as stale echoes, not as user changes.
constexpr uint64_t kEchoWindowMs = 300;

uint64_t NowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string TargetString(std::optional<uint64_t> serial) {
    return serial ? std::to_string(*serial) : std::string("-1");
}

}  // namespace

const char* RoleName(ChatMixRole role) {
    return role == ChatMixRole::Game ? "Game" : "Chat";
}

float PercentToLinear(uint32_t percent) {
    float v = static_cast<float>(percent) / 100.0f;
    return v * v * v;
}

uint32_t LinearToPercent(float linear) {
    if (linear <= 0.0f) return 0;
    return static_cast<uint32_t>(std::lround(std::cbrt(linear) * 100.0f));
}

ChatMixEngine::ChatMixEngine(sd_event* event, PipeWireRegistry& registry,
                             VolumeCallback external_volume_change)
    : registry_(registry),
      external_volume_change_(std::move(external_volume_change)),
      recheck_timer_(event, [this] {
          for (Role* role : {&game_, &chat_}) {
              CheckVolume(*role, role->sink_node, true);
              CheckVolume(*role, role->output_node, true);
          }
      }) {}

ChatMixEngine::~ChatMixEngine() {
    Unload();
}

std::string ChatMixEngine::SinkName(ChatMixRole role) {
    return role == ChatMixRole::Game ? "priomix-game" : "priomix-chat";
}

std::string ChatMixEngine::OutputName(ChatMixRole role) {
    return SinkName(role) + ".output";
}

pw_impl_module* ChatMixEngine::LoadRole(ChatMixRole role,
                                        std::optional<uint64_t> target) {
    // Argument syntax: SPA-JSON as documented for libpipewire-module-loopback. Stream
    // names are given explicitly instead of relying on the "input."/"output." defaults.
    const std::string sink = SinkName(role);
    const std::string label = std::string("priomix ") + RoleName(role);
    std::string args =
        "{"
        " node.description = \"" +
        label +
        "\""
        " audio.position = [ FL FR ]"
        " capture.props = {"
        "   node.name = \"" +
        sink +
        "\""
        "   node.description = \"" +
        label +
        "\""
        "   media.class = \"Audio/Sink\""
        // WirePlumber's node/state-stream.lua also restores virtual Audio/* nodes
        // without device routes; the daemon owns this volume (config.json).
        "   state.restore-props = false"
        " }"
        " playback.props = {"
        "   node.name = \"" +
        OutputName(role) +
        "\""
        "   node.description = \"" +
        label +
        " (output)\""
        "   node.passive = true"
        "   node.dont-fallback = true"
        "   node.linger = true"
        "   state.restore-props = false"
        "   state.restore-target = false"
        "   target.object = \"" +
        TargetString(target) +
        "\""
        " }"
        "}";
    pw_impl_module* module = registry_.LoadModule("libpipewire-module-loopback", args);
    if (module != nullptr) {
        PRIOMIX_LOG_INFO("Loaded loopback module for role %s", RoleName(role));
    } else {
        PRIOMIX_LOG_ERROR("Failed to load libpipewire-module-loopback for role %s",
                          RoleName(role));
    }
    return module;
}

bool ChatMixEngine::Load(std::optional<uint64_t> game_target,
                         std::optional<uint64_t> chat_target) {
    if (game_.module == nullptr) {
        game_.target = TargetString(game_target);
        game_.module = LoadRole(ChatMixRole::Game, game_target);
    }
    if (chat_.module == nullptr) {
        chat_.target = TargetString(chat_target);
        chat_.module = LoadRole(ChatMixRole::Chat, chat_target);
    }
    return IsLoaded();
}

void ChatMixEngine::Unload() {
    for (Role* role : {&game_, &chat_}) {
        PipeWireRegistry::UnloadModule(role->module);
        role->module = nullptr;
        role->sink_node = 0;
        role->output_node = 0;
        role->applied_target.clear();
        role->applied_volume.reset();
        role->applied_dampening.reset();
    }
}

void ChatMixEngine::Sync() {
    for (Role* role : {&game_, &chat_}) {
        const PwNode* sink = registry_.FindNodeByName(SinkName(role->role));
        const PwNode* output = registry_.FindNodeByName(OutputName(role->role));
        uint32_t sink_id = sink != nullptr ? sink->id : 0;
        uint32_t output_id = output != nullptr ? output->id : 0;
        if (sink_id != role->sink_node) {
            role->sink_node = sink_id;
            role->applied_volume.reset();
        }
        if (output_id != role->output_node) {
            role->output_node = output_id;
            role->applied_target.clear();
            role->applied_dampening.reset();
        }
        Apply(*role);
    }
}

void ChatMixEngine::Apply(Role& role) {
    if (role.output_node != 0 && role.applied_target != role.target &&
        registry_.HasDefaultMetadata()) {
        if (registry_.SetMetadata(role.output_node, "target.object", "Spa:Id",
                                  role.target.c_str())) {
            role.applied_target = role.target;
            PRIOMIX_LOG_DEBUG("Retargeted role %s to object.serial=%s",
                              RoleName(role.role), role.target.c_str());
        }
    }
    // Volumes are only written once the node has reported its Props, i.e. is fully set
    // up; the first report then triggers the initial write (see CheckVolume()).
    if (HasReportedVolume(role.sink_node) && role.applied_volume != role.volume) {
        if (registry_.SetNodeVolume(role.sink_node, PercentToLinear(role.volume))) {
            role.applied_volume = role.volume;
            role.last_volume_set_ms = NowMs();
            PRIOMIX_LOG_DEBUG("Set %s volume to %u%%", RoleName(role.role), role.volume);
        }
    }
    // Without a target WirePlumber only stops relinking but keeps an existing link (see
    // "waiting for defined target" in find-defined-target.lua), so audio would continue
    // to reach e.g. a device that was just ignored. Silence the stream instead.
    uint32_t dampening = role.target == "-1" ? 0 : role.dampening;
    if (HasReportedVolume(role.output_node) && role.applied_dampening != dampening) {
        if (registry_.SetNodeVolume(role.output_node, PercentToLinear(dampening))) {
            role.applied_dampening = dampening;
            role.last_dampening_set_ms = NowMs();
            PRIOMIX_LOG_DEBUG("Set %s dampening to %u%%", RoleName(role.role), dampening);
        }
    }
}

bool ChatMixEngine::HasReportedVolume(uint32_t node_id) const {
    const PwNode* node = node_id != 0 ? registry_.FindNode(node_id) : nullptr;
    return node != nullptr && !node->channel_volumes.empty();
}

void ChatMixEngine::OnVolumeChanged(uint32_t node_id) {
    for (Role* role : {&game_, &chat_}) {
        if (node_id == role->sink_node || node_id == role->output_node) {
            CheckVolume(*role, node_id, false);
            return;
        }
    }
}

void ChatMixEngine::CheckVolume(Role& role, uint32_t node_id, bool settled) {
    const PwNode* node = node_id != 0 ? registry_.FindNode(node_id) : nullptr;
    if (node == nullptr || node->channel_volumes.empty()) return;
    float loudest =
        *std::max_element(node->channel_volumes.begin(), node->channel_volumes.end());
    uint32_t percent = LinearToPercent(loudest);
    bool is_sink = node_id == role.sink_node;

    std::optional<uint32_t> expected =
        is_sink ? role.applied_volume : role.applied_dampening;
    if (!expected) {
        // First report of a new node: nothing of ours to compare with yet, write our value.
        Apply(role);
        return;
    }
    if (expected == percent) return;
    uint64_t last_set = is_sink ? role.last_volume_set_ms : role.last_dampening_set_ms;
    PRIOMIX_LOG_DEBUG("%s %s reported %u%% (expected %d%%)", RoleName(role.role),
                      is_sink ? "volume" : "dampening", percent,
                      expected ? static_cast<int>(*expected) : -1);
    if (!settled && NowMs() - last_set < kEchoWindowMs) {
        // Possibly a stale echo of an earlier write; decide once the window is over.
        recheck_timer_.StartIfIdle(kEchoWindowMs);
        return;
    }

    if (is_sink) {
        // Level 1 changed from outside (tray, media keys, pavucontrol): adopt it.
        role.volume = percent;
        role.applied_volume = percent;
        if (external_volume_change_) external_volume_change_(role.role, percent);
    } else {
        // Level 2 belongs to the wheel/hotkeys only: enforce our value.
        role.applied_dampening.reset();
        Apply(role);
    }
}

void ChatMixEngine::Retarget(ChatMixRole role, std::optional<uint64_t> serial) {
    Role& r = Get(role);
    r.target = TargetString(serial);
    Apply(r);
}

void ChatMixEngine::SetVolume(ChatMixRole role, uint32_t percent) {
    Role& r = Get(role);
    r.volume = std::min<uint32_t>(percent, 150);
    Apply(r);
}

uint32_t ChatMixEngine::Volume(ChatMixRole role) const {
    return Get(role).volume;
}

void ChatMixEngine::SetBalance(uint32_t balance) {
    Dampening d = ComputeDampening(balance);
    game_.dampening = d.game;
    chat_.dampening = d.chat;
    Apply(game_);
    Apply(chat_);
}

}  // namespace priomix
