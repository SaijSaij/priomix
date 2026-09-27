// SPDX-License-Identifier: GPL-3.0-only
#include "config_store.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include "json.hpp"
#include "logging.hpp"

namespace priomix {

namespace {

constexpr char kKeySeparator = '@';

json::Value ListToJson(const PriorityList& list) {
    json::Array array;
    for (const auto& entry : list) {
        json::Object object{{"id", entry.id}};
        if (!entry.disambiguation_path.empty()) {
            object.emplace_back("disambiguation_path", entry.disambiguation_path);
        }
        array.emplace_back(std::move(object));
    }
    return array;
}

PriorityList ListFromJson(const json::Value* value) {
    PriorityList list;
    if (value == nullptr) return list;
    for (const auto& item : value->AsArray()) {
        PriorityEntry entry;
        if (item.IsString()) {
            // Tolerate hand-edited configs that use plain strings.
            entry.id = item.AsString();
        } else if (const auto* id = item.Find("id")) {
            entry.id = id->AsString();
            if (const auto* path = item.Find("disambiguation_path")) {
                entry.disambiguation_path = path->AsString();
            }
        }
        if (entry.id.empty()) continue;
        if (std::find(list.begin(), list.end(), entry) != list.end()) continue;
        list.push_back(std::move(entry));
    }
    return list;
}

json::Value StringsToJson(const std::vector<std::string>& strings) {
    json::Array array;
    for (const auto& s : strings) array.emplace_back(s);
    return array;
}

std::vector<std::string> StringsFromJson(const json::Value* value) {
    std::vector<std::string> strings;
    if (value == nullptr) return strings;
    for (const auto& item : value->AsArray()) {
        if (item.IsString() && !item.AsString().empty())
            strings.push_back(item.AsString());
    }
    return strings;
}

uint32_t UintFromJson(const json::Value& root, const char* key, uint32_t fallback,
                      uint32_t min, uint32_t max) {
    const auto* value = root.Find(key);
    if (value == nullptr || !value->IsNumber()) return fallback;
    double n = value->AsNumber();
    if (n < min) return min;
    if (n > max) return max;
    return static_cast<uint32_t>(n);
}

}  // namespace

std::string PriorityEntry::Key() const {
    if (disambiguation_path.empty()) return id;
    return id + kKeySeparator + disambiguation_path;
}

PriorityEntry PriorityEntry::FromKey(const std::string& key) {
    auto pos = key.find(kKeySeparator);
    if (pos == std::string::npos) return {key, {}};
    return {key.substr(0, pos), key.substr(pos + 1)};
}

std::filesystem::path DefaultConfigDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && xdg[0] == '/')
        return std::filesystem::path(xdg) / "priomix-daemon";
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : "/tmp") / ".config" /
           "priomix-daemon";
}

bool ReadFile(const std::filesystem::path& path, std::string& content) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    return true;
}

bool WriteFileAtomically(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        PRIOMIX_LOG_ERROR("Cannot create %s: %s", path.parent_path().c_str(),
                          ec.message().c_str());
        return false;
    }

    const std::string tmp = path.string() + ".tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        PRIOMIX_LOG_ERROR("Cannot write %s: %s", tmp.c_str(), std::strerror(errno));
        return false;
    }
    const char* data = content.data();
    size_t left = content.size();
    while (left > 0) {
        ssize_t n = ::write(fd, data, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            PRIOMIX_LOG_ERROR("Cannot write %s: %s", tmp.c_str(), std::strerror(errno));
            ::close(fd);
            ::unlink(tmp.c_str());
            return false;
        }
        data += n;
        left -= static_cast<size_t>(n);
    }
    ::fsync(fd);
    ::close(fd);
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        PRIOMIX_LOG_ERROR("Cannot replace %s: %s", path.c_str(), std::strerror(errno));
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

ConfigStore::ConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

bool ConfigStore::Load(Config& config) const {
    std::string text;
    if (!ReadFile(path_, text)) return false;

    std::string error;
    auto root = json::Parse(text, &error);
    if (!root || !root->IsObject()) {
        PRIOMIX_LOG_ERROR("Ignoring malformed %s: %s", path_.c_str(),
                          root ? "not an object" : error.c_str());
        return false;
    }

    Config defaults;
    config.game_sink_priority = ListFromJson(root->Find("game_sink_priority"));
    config.chat_sink_priority = ListFromJson(root->Find("chat_sink_priority"));
    config.source_priority = ListFromJson(root->Find("source_priority"));
    config.ignored_sinks = StringsFromJson(root->Find("ignored_sinks"));
    config.ignored_sources = StringsFromJson(root->Find("ignored_sources"));
    if (const auto* sync = root->Find("sync_priority_lists")) {
        config.sync_priority_lists = sync->AsBool(defaults.sync_priority_lists);
    }
    config.game_volume = UintFromJson(*root, "game_volume", defaults.game_volume, 0, 150);
    config.chat_volume = UintFromJson(*root, "chat_volume", defaults.chat_volume, 0, 150);
    config.chatmix_balance =
        UintFromJson(*root, "chatmix_balance", defaults.chatmix_balance, 0, 100);
    config.headset_poll_interval_ac_ms =
        UintFromJson(*root, "headset_poll_interval_ac_ms",
                     defaults.headset_poll_interval_ac_ms, 500, 3600000);
    config.headset_poll_interval_battery_ms =
        UintFromJson(*root, "headset_poll_interval_battery_ms",
                     defaults.headset_poll_interval_battery_ms, 500, 3600000);
    return true;
}

bool ConfigStore::Save(const Config& config) const {
    json::Object root{
        {"game_sink_priority", ListToJson(config.game_sink_priority)},
        {"chat_sink_priority", ListToJson(config.chat_sink_priority)},
        {"sync_priority_lists", config.sync_priority_lists},
        {"source_priority", ListToJson(config.source_priority)},
        {"ignored_sinks", StringsToJson(config.ignored_sinks)},
        {"ignored_sources", StringsToJson(config.ignored_sources)},
        {"game_volume", config.game_volume},
        {"chat_volume", config.chat_volume},
        {"chatmix_balance", config.chatmix_balance},
        {"headset_poll_interval_ac_ms", config.headset_poll_interval_ac_ms},
        {"headset_poll_interval_battery_ms", config.headset_poll_interval_battery_ms},
    };
    return WriteFileAtomically(path_, json::Value(std::move(root)).Serialize());
}

}  // namespace priomix
