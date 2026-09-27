// SPDX-License-Identifier: GPL-3.0-only
#include "device_cache.hpp"

#include "config_store.hpp"
#include "json.hpp"
#include "logging.hpp"

namespace priomix {

DeviceCache::DeviceCache(std::filesystem::path path) : path_(std::move(path)) {}

void DeviceCache::Load() {
    names_.clear();
    dirty_ = false;
    std::string text;
    if (!ReadFile(path_, text)) return;
    std::string error;
    auto root = json::Parse(text, &error);
    if (!root || !root->IsObject()) {
        PRIOMIX_LOG_WARN("Ignoring malformed %s: %s", path_.c_str(), error.c_str());
        return;
    }
    for (const auto& [key, value] : root->AsObject()) {
        if (value.IsString()) names_[key] = value.AsString();
    }
}

void DeviceCache::SaveIfDirty() {
    if (!dirty_) return;
    json::Object root;
    for (const auto& [key, name] : names_) root.emplace_back(key, name);
    if (WriteFileAtomically(path_, json::Value(std::move(root)).Serialize()))
        dirty_ = false;
}

bool DeviceCache::Update(const std::string& key, const std::string& name) {
    if (name.empty()) return false;
    auto& stored = names_[key];
    if (stored == name) return false;
    stored = name;
    dirty_ = true;
    return true;
}

std::string DeviceCache::NameOf(const std::string& key) const {
    auto it = names_.find(key);
    return it != names_.end() ? it->second : std::string();
}

}  // namespace priomix
