// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Two always-present virtual sinks, "priomix-game" and "priomix-chat", each built from
// one libpipewire-module-loopback instance loaded into the daemon's own PipeWire context:
//
//   apps -> [priomix-<role>]          capture side, media.class Audio/Sink
//                                     level 1: normal volume (tray, keyboard, GUI fader)
//        -> [priomix-<role>.output]   playback stream, target.object = physical device
//                                     level 2: ChatMix dampening (wheel / hotkeys only)
//        -> physical output device
//
// Switching the physical device is done live by writing "target.object" (object.serial,
// type "Spa:Id") for the playback node into the "default" metadata, exactly like
// pipewire-pulse does for pavucontrol "move" requests; WirePlumber's
// linking/find-defined-target.lua picks it up. The playback stream is created with
// node.dont-fallback + node.linger, so while no target is set ("-1") WirePlumber waits
// instead of linking it to the default sink (= our own game sink).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

struct pw_impl_module;

namespace priomix {

class PipeWireRegistry;

enum class ChatMixRole { Game, Chat };

const char* RoleName(ChatMixRole role);

class ChatMixEngine {
public:
    using VolumeCallback = std::function<void(ChatMixRole role, uint32_t volume)>;

    ChatMixEngine(PipeWireRegistry& registry, VolumeCallback external_volume_change);
    ~ChatMixEngine();

    ChatMixEngine(const ChatMixEngine&) = delete;
    ChatMixEngine& operator=(const ChatMixEngine&) = delete;

    // Loads both loopback modules. Initial targets avoid a first link to a wrong device.
    bool Load(std::optional<uint64_t> game_target, std::optional<uint64_t> chat_target);
    void Unload();
    bool IsLoaded() const { return game_.module != nullptr && chat_.module != nullptr; }

    // Re-resolves our node IDs and applies pending targets/volumes. Call on every graph
    // change.
    void Sync();
    // Call when the registry reports a volume change on any of our nodes.
    void OnVolumeChanged(uint32_t node_id);

    // std::nullopt = no physical device available.
    void Retarget(ChatMixRole role, std::optional<uint64_t> serial);
    void SetVolume(ChatMixRole role, uint32_t percent);
    uint32_t Volume(ChatMixRole role) const;
    void SetBalance(uint32_t balance);

    static std::string SinkName(ChatMixRole role);
    static std::string OutputName(ChatMixRole role);

private:
    struct Role {
        explicit Role(ChatMixRole r) : role(r) {}
        ChatMixRole role;
        pw_impl_module* module = nullptr;
        uint32_t sink_node = 0;  // 0 = not (yet) in the registry
        uint32_t output_node = 0;
        std::string target = "-1";
        std::string applied_target;
        uint32_t volume = 100;
        std::optional<uint32_t> applied_volume;
        uint32_t dampening = 100;
        std::optional<uint32_t> applied_dampening;
        uint64_t last_volume_set_ms = 0;
        uint64_t last_dampening_set_ms = 0;
    };

    Role& Get(ChatMixRole role) { return role == ChatMixRole::Game ? game_ : chat_; }
    const Role& Get(ChatMixRole role) const {
        return role == ChatMixRole::Game ? game_ : chat_;
    }
    pw_impl_module* LoadRole(ChatMixRole role, std::optional<uint64_t> target);
    void Apply(Role& role);

    PipeWireRegistry& registry_;
    VolumeCallback external_volume_change_;
    Role game_{ChatMixRole::Game};
    Role chat_{ChatMixRole::Chat};
};

// Plasma/pavucontrol show volumes on a cubic scale; PipeWire channelVolumes are linear.
float PercentToLinear(uint32_t percent);
uint32_t LinearToPercent(float linear);

}  // namespace priomix
