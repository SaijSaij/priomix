// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Owns ~/.config/priomix-daemon/config.json. Only stable IDs are stored here, display
// names live in the separate device cache (device_cache.hpp).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace priomix {

struct PriorityEntry {
    std::string id;
    // Only filled after two identical devices were connected at the same time.
    std::string disambiguation_path;

    // Opaque key used on D-Bus and in the ignore lists: "<id>" or "<id>@<path>".
    std::string Key() const;
    static PriorityEntry FromKey(const std::string& key);

    bool operator==(const PriorityEntry&) const = default;
};

using PriorityList = std::vector<PriorityEntry>;

struct Config {
    PriorityList game_sink_priority;
    PriorityList chat_sink_priority;
    bool sync_priority_lists = true;
    PriorityList source_priority;
    std::vector<std::string> ignored_sinks;
    std::vector<std::string> ignored_sources;
    uint32_t game_volume = 100;
    uint32_t chat_volume = 100;
    uint32_t chatmix_balance = 50;
    uint32_t headset_poll_interval_ac_ms = 5000;
    uint32_t headset_poll_interval_battery_ms = 30000;
};

class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path path);

    // Returns false if the file did not exist (first start) or could not be parsed; the
    // config is then left at defaults.
    bool Load(Config& config) const;
    bool Save(const Config& config) const;

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

// $XDG_CONFIG_HOME/priomix-daemon (falls back to ~/.config/priomix-daemon).
std::filesystem::path DefaultConfigDir();

// Writes via temporary file + rename so a crash never leaves a truncated file.
bool WriteFileAtomically(const std::filesystem::path& path, const std::string& content);
bool ReadFile(const std::filesystem::path& path, std::string& content);

}  // namespace priomix
