// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Test-side PipeWire client on its own pw_thread_loop: creates and removes null sinks /
// sources ("plugging" devices), reads the default metadata, changes node volumes like a
// volume applet would, plays a sine tone into a sink and measures the level that arrives
// at a sink's monitor.

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

struct pw_core;
struct pw_context;
struct pw_proxy;
struct pw_registry;
struct pw_thread_loop;
struct spa_dict;

namespace priomix::test {

class PwGraph {
public:
    PwGraph();
    ~PwGraph();

    PwGraph(const PwGraph&) = delete;
    PwGraph& operator=(const PwGraph&) = delete;

    bool Connect();

    // media_class "Audio/Sink" or "Audio/Source". The node lives as long as this client
    // keeps its proxy, RemoveDevice() "unplugs" it.
    bool AddDevice(const std::string& name, const std::string& media_class);
    void RemoveDevice(const std::string& name);

    std::optional<uint32_t> NodeId(const std::string& name);
    std::optional<uint64_t> NodeSerial(const std::string& name);
    // Value of a key in the "default" metadata, subject 0 unless given.
    std::optional<std::string> Metadata(const std::string& key, uint32_t subject = 0);

    // Sets channelVolumes (linear) of any node, e.g. what the Plasma tray does.
    bool SetNodeVolume(const std::string& name, float linear);
    // Current first channelVolumes entry (linear) of a node.
    std::optional<float> NodeVolume(const std::string& name);

    // Plays a 440 Hz sine with the given amplitude into the named sink until stopped.
    bool StartTone(const std::string& sink, float amplitude = 0.5f);
    void StopTones();

    // Starts metering the monitor of the named sink.
    bool StartMeter(const std::string& sink);
    // RMS measured at the meter of `sink` over the next `ms` milliseconds.
    double MeasureRms(const std::string& sink, int ms = 400);

private:
    struct Node {
        uint32_t id = 0;
        uint64_t serial = 0;
        std::string name;
    };
    struct Stream;

    static void OnGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                         uint32_t version, const spa_dict* props);
    static void OnGlobalRemove(void* data, uint32_t id);
    static int OnMetadataProperty(void* data, uint32_t subject, const char* key,
                                  const char* type, const char* value);
    bool Sync();
    std::unique_ptr<Stream> CreateStream(const std::string& target, bool capture,
                                         float amplitude);

    pw_thread_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_registry* registry_ = nullptr;
    struct Hooks;
    std::unique_ptr<Hooks> hooks_;
    pw_proxy* metadata_ = nullptr;

    std::map<uint32_t, Node> nodes_;                              // guarded by loop lock
    std::map<std::pair<uint32_t, std::string>, std::string> md_;  // guarded by loop lock
    std::map<std::string, pw_proxy*> devices_;
    std::map<std::string, std::unique_ptr<Stream>> tones_;
    std::map<std::string, std::unique_ptr<Stream>> meters_;
};

}  // namespace priomix::test
