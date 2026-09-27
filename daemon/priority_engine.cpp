// SPDX-License-Identifier: GPL-3.0-only
#include "priority_engine.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace priomix {

namespace {

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// Unique entries with the given primary ID across all lists, in first-seen order.
std::vector<PriorityEntry> Candidates(const std::string& id,
                                      const std::vector<PriorityList*>& lists) {
    std::vector<PriorityEntry> candidates;
    for (const auto* list : lists) {
        for (const auto& entry : *list) {
            if (entry.id == id && std::find(candidates.begin(), candidates.end(),
                                            entry) == candidates.end()) {
                candidates.push_back(entry);
            }
        }
    }
    return candidates;
}

void InsertFront(const PriorityEntry& entry, const std::vector<PriorityList*>& lists) {
    for (auto* list : lists) {
        if (std::find(list->begin(), list->end(), entry) == list->end()) {
            list->insert(list->begin(), entry);
        }
    }
}

void Rename(const PriorityEntry& from, const PriorityEntry& to,
            const std::vector<PriorityList*>& lists,
            const std::vector<std::vector<std::string>*>& ignored) {
    for (auto* list : lists) {
        for (auto& entry : *list) {
            if (entry == from) entry = to;
        }
    }
    for (auto* keys : ignored) {
        for (auto& key : *keys) {
            if (key == from.Key()) key = to.Key();
        }
    }
}

}  // namespace

bool ResolveKeys(std::vector<KeyRequest>& devices,
                 const std::vector<PriorityList*>& lists,
                 const std::vector<std::vector<std::string>*>& ignored) {
    bool modified = false;

    std::map<std::string, std::vector<KeyRequest*>> groups;
    for (auto& device : devices) groups[device.id].push_back(&device);

    for (auto& [id, group] : groups) {
        auto candidates = Candidates(id, lists);

        if (group.size() == 1) {
            // No collision: the disambiguation path is only a tie breaker.
            KeyRequest& device = *group.front();
            const PriorityEntry* match = nullptr;
            for (const auto& c : candidates) {
                if (!c.disambiguation_path.empty() &&
                    c.disambiguation_path == device.bus_path) {
                    match = &c;
                    break;
                }
            }
            for (const auto& c : candidates) {
                if (match == nullptr && c.disambiguation_path.empty()) match = &c;
            }
            if (match == nullptr && !candidates.empty()) match = &candidates.front();
            if (match == nullptr) {
                PriorityEntry entry{id, {}};
                InsertFront(entry, lists);
                device.key = entry.Key();
                modified = true;
            } else {
                device.key = match->Key();
            }
            continue;
        }

        // Collision (identical devices without serial): best effort via bus path. If even
        // the bus paths are equal (several nodes of one device), fall back to node.name.
        std::map<std::string, int> path_count;
        for (auto* device : group) ++path_count[device->bus_path];
        std::vector<std::string> disambiguation;
        for (auto* device : group) {
            bool unique = !device->bus_path.empty() && path_count[device->bus_path] == 1;
            disambiguation.push_back(unique ? device->bus_path : device->node_name);
        }

        std::set<std::string> claimed;
        std::vector<bool> resolved(group.size(), false);
        for (size_t i = 0; i < group.size(); ++i) {
            for (const auto& c : candidates) {
                if (c.disambiguation_path == disambiguation[i] &&
                    !claimed.count(c.Key())) {
                    group[i]->key = c.Key();
                    claimed.insert(c.Key());
                    resolved[i] = true;
                    break;
                }
            }
        }
        for (size_t i = 0; i < group.size(); ++i) {
            if (resolved[i]) continue;
            for (const auto& c : candidates) {
                if (c.disambiguation_path.empty() && !claimed.count(c.Key())) {
                    PriorityEntry renamed{id, disambiguation[i]};
                    Rename(c, renamed, lists, ignored);
                    claimed.insert(c.Key());
                    claimed.insert(renamed.Key());
                    group[i]->key = renamed.Key();
                    resolved[i] = true;
                    modified = true;
                    break;
                }
            }
            if (!resolved[i]) {
                PriorityEntry entry{id, disambiguation[i]};
                InsertFront(entry, lists);
                claimed.insert(entry.Key());
                group[i]->key = entry.Key();
                modified = true;
            }
        }
    }
    return modified;
}

std::optional<std::string> PickWinner(
    const PriorityList& list, const std::vector<std::string>& ignored,
    const std::function<bool(const std::string&)>& available) {
    for (const auto& entry : list) {
        std::string key = entry.Key();
        if (Contains(ignored, key)) continue;
        if (available(key)) return key;
    }
    return std::nullopt;
}

PriorityList Reorder(const PriorityList& current, const std::vector<std::string>& keys) {
    std::set<std::string> known;
    for (const auto& entry : current) known.insert(entry.Key());

    PriorityList result;
    std::set<std::string> seen;
    for (const auto& key : keys) {
        if (!known.count(key) || !seen.insert(key).second) continue;
        result.push_back(PriorityEntry::FromKey(key));
    }
    for (const auto& entry : current) {
        if (seen.insert(entry.Key()).second) result.push_back(entry);
    }
    return result;
}

std::vector<std::string> Keys(const PriorityList& list) {
    std::vector<std::string> keys;
    keys.reserve(list.size());
    for (const auto& entry : list) keys.push_back(entry.Key());
    return keys;
}

Dampening ComputeDampening(uint32_t balance) {
    balance = std::min<uint32_t>(balance, 100);
    if (balance > 50) return {100, 100 - (balance - 50) * 2};
    if (balance < 50) return {100 - (50 - balance) * 2, 100};
    return {100, 100};
}

uint32_t BalanceFromWheelLevel(int level) {
    level = std::clamp(level, 0, 128);
    return static_cast<uint32_t>(std::lround((128.0 - level) * 100.0 / 128.0));
}

}  // namespace priomix
