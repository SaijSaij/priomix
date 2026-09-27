// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Owns ~/.config/priomix-daemon/known_devices.json: device key -> last seen display
// name. Pure cache for the GUI, never influences priority decisions.

#include <filesystem>
#include <map>
#include <string>

namespace priomix {

class DeviceCache {
public:
    explicit DeviceCache(std::filesystem::path path);

    void Load();
    // Writes only if something changed since the last load/save.
    void SaveIfDirty();

    // Returns true if the stored name changed.
    bool Update(const std::string& key, const std::string& name);
    std::string NameOf(const std::string& key) const;

private:
    std::filesystem::path path_;
    std::map<std::string, std::string> names_;
    bool dirty_ = false;
};

}  // namespace priomix
