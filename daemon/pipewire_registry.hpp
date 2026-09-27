// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Native libpipewire connection, driven from the sd-event main loop: the pw_loop is not
// run in its own thread, instead its fd is polled by sd-event and pw_loop_iterate() is
// called when it becomes readable (pw_loop_get_fd(), see pipewire/loop.h).
//
// Tracks audio sinks/sources, audio devices, our own priomix nodes and the "default"
// metadata object. Everything happens on the main thread.

#include <systemd/sd-event.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "device_id.hpp"

struct pw_context;
struct pw_core;
struct pw_impl_module;
struct pw_loop;
struct pw_device_info;
struct pw_node_info;
struct pw_registry;
struct spa_dict;
struct spa_pod;

namespace priomix {

struct PwNode {
    uint32_t id = 0;
    uint64_t serial = 0;
    uint32_t device_id = 0;  // SPA_ID_INVALID if the node has no device
    std::string media_class;
    Properties props;
    bool info_received = false;
    std::vector<float> channel_volumes;  // from SPA_PARAM_Props, own nodes only

    std::string Name() const;
};

struct PwDevice {
    uint32_t id = 0;
    Properties props;
};

class PipeWireRegistry {
public:
    struct Callbacks {
        std::function<void()> ready;          // initial enumeration complete
        std::function<void()> graph_changed;  // node/device added, removed or changed
        std::function<void(uint32_t node_id)> volume_changed;
        std::function<void()> fatal_error;  // connection to PipeWire lost
    };

    explicit PipeWireRegistry(Callbacks callbacks);
    ~PipeWireRegistry();

    PipeWireRegistry(const PipeWireRegistry&) = delete;
    PipeWireRegistry& operator=(const PipeWireRegistry&) = delete;

    bool Connect(sd_event* event);
    bool IsReady() const { return ready_; }

    const std::map<uint32_t, PwNode>& Nodes() const { return nodes_; }
    const PwNode* FindNode(uint32_t id) const;
    const PwNode* FindNodeByName(const std::string& name) const;
    const PwDevice* FindDevice(uint32_t id) const;

    // Sets all channel volumes of a node (linear gain) via SPA_PARAM_Props.
    bool SetNodeVolume(uint32_t node_id, float linear);

    bool HasDefaultMetadata() const;
    bool SetMetadata(uint32_t subject, const char* key, const char* type,
                     const char* value);

    // Loads a PipeWire module into this client's context (e.g. module-loopback).
    pw_impl_module* LoadModule(const char* name, const std::string& args);
    static void UnloadModule(pw_impl_module* module);

    // Node names starting with this prefix belong to priomix itself.
    static constexpr const char* kOwnNodePrefix = "priomix-";

private:
    struct Bound;

    static void OnGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                         uint32_t version, const spa_dict* props);
    static void OnGlobalRemove(void* data, uint32_t id);
    static void OnCoreDone(void* data, uint32_t id, int seq);
    static void OnCoreError(void* data, uint32_t id, int seq, int res,
                            const char* message);
    static int OnLoopFd(sd_event_source* source, int fd, uint32_t revents,
                        void* userdata);
    static void OnNodeInfo(void* data, const pw_node_info* info);
    static void OnNodeParam(void* data, int seq, uint32_t id, uint32_t index,
                            uint32_t next, const spa_pod* param);
    static void OnDeviceInfo(void* data, const pw_device_info* info);

    void BindNode(uint32_t id, uint32_t version, const spa_dict* props);
    void BindDevice(uint32_t id, uint32_t version);
    void BindMetadata(uint32_t id, uint32_t version);
    void RemoveBound(uint32_t id);
    void NotifyChanged();

    Callbacks callbacks_;
    pw_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_registry* registry_ = nullptr;
    sd_event_source* loop_source_ = nullptr;
    struct Hooks;
    std::unique_ptr<Hooks> hooks_;

    std::map<uint32_t, std::unique_ptr<Bound>> bound_;
    std::map<uint32_t, PwNode> nodes_;
    std::map<uint32_t, PwDevice> devices_;
    std::optional<uint32_t> metadata_id_;

    int sync_seq_ = 0;
    int sync_round_ = 0;
    bool ready_ = false;
};

}  // namespace priomix
