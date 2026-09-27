// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Pure decision logic, no PipeWire or D-Bus dependency (unit tested in tests/).

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "config_store.hpp"

namespace priomix {

// A device (PipeWire node) that is currently present, as input for key resolution.
struct KeyRequest {
    std::string id;         // primary stable ID
    std::string bus_path;   // device.bus-path
    std::string node_name;  // fallback disambiguation if bus paths are equal too
    std::string key;        // output: the priority entry key this device maps to
};

// Maps every present device to a priority entry and updates the lists accordingly:
//  - unknown devices are inserted at position 1 of every list in `lists`,
//  - on a collision (several present devices with the same primary ID) entries get a
//    disambiguation_path; an existing entry without path is taken over first.
// `ignored` key lists are rewritten when an entry key changes.
// Returns true if any list was modified.
bool ResolveKeys(std::vector<KeyRequest>& devices,
                 const std::vector<PriorityList*>& lists,
                 const std::vector<std::vector<std::string>*>& ignored);

// First entry that is not ignored and available, or std::nullopt.
std::optional<std::string> PickWinner(
    const PriorityList& list, const std::vector<std::string>& ignored,
    const std::function<bool(const std::string&)>& available);

// Returns `current` reordered to follow `keys`. Unknown keys are ignored, entries of
// `current` missing in `keys` are appended in their previous order (never lost).
PriorityList Reorder(const PriorityList& current, const std::vector<std::string>& keys);

std::vector<std::string> Keys(const PriorityList& list);

// ChatMix: asymmetric dampening like the SteelSeries wheel (balance 0..100, 50 = center,
// above 50 favours game). Returns {game_percent, chat_percent}.
struct Dampening {
    uint32_t game;
    uint32_t chat;
};
Dampening ComputeDampening(uint32_t balance);

// HeadsetControl reports the wheel as 0..128 with < 64 meaning "more game".
uint32_t BalanceFromWheelLevel(int level);

}  // namespace priomix
