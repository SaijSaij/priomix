// SPDX-License-Identifier: GPL-3.0-only
#include "pipewire_registry.hpp"

#include <pipewire/extensions/metadata.h>
#include <pipewire/impl-module.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/result.h>

#include <cerrno>
#include <cstring>

#include "logging.hpp"

namespace priomix {

namespace {

Properties ToProperties(const spa_dict* dict) {
    Properties props;
    if (dict == nullptr) return props;
    const spa_dict_item* item;
    spa_dict_for_each(item, dict) {
        if (item->key != nullptr && item->value != nullptr)
            props[item->key] = item->value;
    }
    return props;
}

const char* Lookup(const spa_dict* dict, const char* key) {
    return dict != nullptr ? spa_dict_lookup(dict, key) : nullptr;
}

bool StartsWith(const char* s, const char* prefix) {
    return s != nullptr && std::strncmp(s, prefix, std::strlen(prefix)) == 0;
}

// Audio endpoints we manage. "Audio/Source/Virtual" and similar sub classes are skipped
// on purpose, they are not physical microphones.
bool IsAudioEndpoint(const char* media_class) {
    return media_class != nullptr && (std::strcmp(media_class, "Audio/Sink") == 0 ||
                                      std::strcmp(media_class, "Audio/Source") == 0);
}

uint64_t ParseU64(const std::string& s, uint64_t fallback) {
    if (s.empty()) return fallback;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    return (end != s.c_str() && *end == '\0') ? v : fallback;
}

}  // namespace

std::string PwNode::Name() const {
    auto it = props.find(PW_KEY_NODE_NAME);
    return it != props.end() ? it->second : std::string();
}

// One bound proxy (node, device or metadata) with its listeners.
struct PipeWireRegistry::Bound {
    PipeWireRegistry* self = nullptr;
    uint32_t id = 0;
    enum class Kind { Node, Device, Metadata } kind = Kind::Node;
    pw_proxy* proxy = nullptr;
    spa_hook proxy_listener{};
    spa_hook object_listener{};
    bool own_node = false;
};

struct PipeWireRegistry::Hooks {
    spa_hook core_listener{};
    spa_hook registry_listener{};
    pw_core_events core_events{};
    pw_registry_events registry_events{};
    pw_node_events node_events{};
    pw_device_events device_events{};
};

PipeWireRegistry::PipeWireRegistry(Callbacks callbacks)
    : callbacks_(std::move(callbacks)), hooks_(std::make_unique<Hooks>()) {
    pw_init(nullptr, nullptr);

    hooks_->core_events.version = PW_VERSION_CORE_EVENTS;
    hooks_->core_events.done = &PipeWireRegistry::OnCoreDone;
    hooks_->core_events.error = &PipeWireRegistry::OnCoreError;

    hooks_->registry_events.version = PW_VERSION_REGISTRY_EVENTS;
    hooks_->registry_events.global = &PipeWireRegistry::OnGlobal;
    hooks_->registry_events.global_remove = &PipeWireRegistry::OnGlobalRemove;

    hooks_->node_events.version = PW_VERSION_NODE_EVENTS;
    hooks_->node_events.info = &PipeWireRegistry::OnNodeInfo;
    hooks_->node_events.param = &PipeWireRegistry::OnNodeParam;

    hooks_->device_events.version = PW_VERSION_DEVICE_EVENTS;
    hooks_->device_events.info = &PipeWireRegistry::OnDeviceInfo;
}

PipeWireRegistry::~PipeWireRegistry() {
    while (!bound_.empty()) RemoveBound(bound_.begin()->first);
    if (registry_ != nullptr) {
        spa_hook_remove(&hooks_->registry_listener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry_));
    }
    if (core_ != nullptr) {
        spa_hook_remove(&hooks_->core_listener);
        pw_core_disconnect(core_);
    }
    if (context_ != nullptr) pw_context_destroy(context_);
    if (loop_source_ != nullptr) sd_event_source_unref(loop_source_);
    if (loop_ != nullptr) {
        pw_loop_leave(loop_);
        pw_loop_destroy(loop_);
    }
    pw_deinit();
}

bool PipeWireRegistry::Connect(sd_event* event) {
    loop_ = pw_loop_new(nullptr);
    if (loop_ == nullptr) {
        PRIOMIX_LOG_ERROR("Cannot create PipeWire loop: %s", std::strerror(errno));
        return false;
    }
    // The loop is iterated from sd-event, so this thread "owns" it permanently.
    pw_loop_enter(loop_);

    context_ = pw_context_new(
        loop_, pw_properties_new(PW_KEY_APP_NAME, "priomix-daemon", nullptr), 0);
    if (context_ == nullptr) {
        PRIOMIX_LOG_ERROR("Cannot create PipeWire context: %s", std::strerror(errno));
        return false;
    }
    core_ = pw_context_connect(context_, nullptr, 0);
    if (core_ == nullptr) {
        PRIOMIX_LOG_ERROR("Cannot connect to PipeWire: %s", std::strerror(errno));
        return false;
    }
    pw_core_add_listener(core_, &hooks_->core_listener, &hooks_->core_events, this);

    registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(registry_, &hooks_->registry_listener,
                             &hooks_->registry_events, this);

    int r = sd_event_add_io(event, &loop_source_, pw_loop_get_fd(loop_), EPOLLIN,
                            &PipeWireRegistry::OnLoopFd, this);
    if (r < 0) {
        PRIOMIX_LOG_ERROR("Cannot watch PipeWire loop fd: %s", std::strerror(-r));
        return false;
    }

    // Round 1 collects all globals, round 2 the info events of the proxies bound in
    // round 1. Only then the graph is complete enough for a first decision.
    sync_seq_ = pw_core_sync(core_, PW_ID_CORE, 0);
    PRIOMIX_LOG_INFO("Connected to PipeWire");
    return true;
}

int PipeWireRegistry::OnLoopFd(sd_event_source*, int, uint32_t, void* userdata) {
    auto* self = static_cast<PipeWireRegistry*>(userdata);
    pw_loop_iterate(self->loop_, 0);
    return 0;
}

void PipeWireRegistry::OnCoreDone(void* data, uint32_t id, int seq) {
    auto* self = static_cast<PipeWireRegistry*>(data);
    if (id != PW_ID_CORE || seq != self->sync_seq_ || self->ready_) return;
    if (++self->sync_round_ < 2) {
        self->sync_seq_ = pw_core_sync(self->core_, PW_ID_CORE, self->sync_seq_);
        return;
    }
    self->ready_ = true;
    PRIOMIX_LOG_DEBUG("Initial PipeWire enumeration complete: %zu node(s), %zu device(s)",
                      self->nodes_.size(), self->devices_.size());
    if (self->callbacks_.ready) self->callbacks_.ready();
}

void PipeWireRegistry::OnCoreError(void* data, uint32_t id, int seq, int res,
                                   const char* message) {
    auto* self = static_cast<PipeWireRegistry*>(data);
    PRIOMIX_LOG_ERROR("PipeWire error: id:%u seq:%d res:%d (%s): %s", id, seq, res,
                      spa_strerror(res), message != nullptr ? message : "");
    if (id == PW_ID_CORE && res == -EPIPE && self->callbacks_.fatal_error) {
        self->callbacks_.fatal_error();
    }
}

void PipeWireRegistry::OnGlobal(void* data, uint32_t id, uint32_t, const char* type,
                                uint32_t version, const spa_dict* props) {
    auto* self = static_cast<PipeWireRegistry*>(data);
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        const char* media_class = Lookup(props, PW_KEY_MEDIA_CLASS);
        const char* name = Lookup(props, PW_KEY_NODE_NAME);
        if (IsAudioEndpoint(media_class) || StartsWith(name, kOwnNodePrefix)) {
            self->BindNode(id, version, props);
        }
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Device) == 0) {
        const char* media_class = Lookup(props, PW_KEY_MEDIA_CLASS);
        if (media_class != nullptr && std::strcmp(media_class, "Audio/Device") == 0) {
            self->BindDevice(id, version);
        }
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        const char* name = Lookup(props, PW_KEY_METADATA_NAME);
        if (name != nullptr && std::strcmp(name, "default") == 0) {
            self->BindMetadata(id, version);
        }
    }
}

void PipeWireRegistry::OnGlobalRemove(void* data, uint32_t id) {
    auto* self = static_cast<PipeWireRegistry*>(data);
    if (self->bound_.count(id) == 0) return;
    bool relevant = self->nodes_.count(id) > 0 || self->devices_.count(id) > 0;
    self->RemoveBound(id);
    if (relevant) self->NotifyChanged();
}

void PipeWireRegistry::BindNode(uint32_t id, uint32_t version, const spa_dict* props) {
    auto bound = std::make_unique<Bound>();
    bound->self = this;
    bound->id = id;
    bound->kind = Bound::Kind::Node;
    bound->own_node = StartsWith(Lookup(props, PW_KEY_NODE_NAME), kOwnNodePrefix);
    bound->proxy = static_cast<pw_proxy*>(
        pw_registry_bind(registry_, id, PW_TYPE_INTERFACE_Node,
                         std::min<uint32_t>(version, PW_VERSION_NODE), 0));
    if (bound->proxy == nullptr) return;

    PwNode& node = nodes_[id];
    node.id = id;
    node.props = ToProperties(props);
    node.media_class =
        Lookup(props, PW_KEY_MEDIA_CLASS) ? Lookup(props, PW_KEY_MEDIA_CLASS) : "";

    pw_node_add_listener(reinterpret_cast<pw_node*>(bound->proxy),
                         &bound->object_listener, &hooks_->node_events, bound.get());
    if (bound->own_node) {
        uint32_t ids[] = {SPA_PARAM_Props};
        pw_node_subscribe_params(reinterpret_cast<pw_node*>(bound->proxy), ids, 1);
    }
    bound_[id] = std::move(bound);
}

void PipeWireRegistry::BindDevice(uint32_t id, uint32_t version) {
    auto bound = std::make_unique<Bound>();
    bound->self = this;
    bound->id = id;
    bound->kind = Bound::Kind::Device;
    bound->proxy = static_cast<pw_proxy*>(
        pw_registry_bind(registry_, id, PW_TYPE_INTERFACE_Device,
                         std::min<uint32_t>(version, PW_VERSION_DEVICE), 0));
    if (bound->proxy == nullptr) return;
    devices_[id].id = id;
    pw_device_add_listener(reinterpret_cast<pw_device*>(bound->proxy),
                           &bound->object_listener, &hooks_->device_events, bound.get());
    bound_[id] = std::move(bound);
}

void PipeWireRegistry::BindMetadata(uint32_t id, uint32_t version) {
    auto bound = std::make_unique<Bound>();
    bound->self = this;
    bound->id = id;
    bound->kind = Bound::Kind::Metadata;
    bound->proxy = static_cast<pw_proxy*>(
        pw_registry_bind(registry_, id, PW_TYPE_INTERFACE_Metadata,
                         std::min<uint32_t>(version, PW_VERSION_METADATA), 0));
    if (bound->proxy == nullptr) return;
    metadata_id_ = id;
    bound_[id] = std::move(bound);
    PRIOMIX_LOG_DEBUG("Bound default metadata (id %u)", id);
    NotifyChanged();
}

void PipeWireRegistry::RemoveBound(uint32_t id) {
    auto it = bound_.find(id);
    if (it == bound_.end()) return;
    Bound& bound = *it->second;
    if (bound.kind != Bound::Kind::Metadata) spa_hook_remove(&bound.object_listener);
    pw_proxy_destroy(bound.proxy);
    if (metadata_id_ == id) metadata_id_.reset();
    nodes_.erase(id);
    devices_.erase(id);
    bound_.erase(it);
}

void PipeWireRegistry::NotifyChanged() {
    if (ready_ && callbacks_.graph_changed) callbacks_.graph_changed();
}

void PipeWireRegistry::OnNodeInfo(void* data, const pw_node_info* info) {
    auto* bound = static_cast<Bound*>(data);
    auto* self = bound->self;
    auto it = self->nodes_.find(bound->id);
    if (it == self->nodes_.end() ||
        (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) == 0) {
        return;
    }

    PwNode& node = it->second;
    Properties props = ToProperties(info->props);
    if (node.info_received && props == node.props) return;
    node.props = std::move(props);
    node.info_received = true;
    node.serial = ParseU64(node.props[PW_KEY_OBJECT_SERIAL], 0);
    node.device_id =
        static_cast<uint32_t>(ParseU64(node.props[PW_KEY_DEVICE_ID], SPA_ID_INVALID));
    node.media_class = node.props[PW_KEY_MEDIA_CLASS];
    self->NotifyChanged();
}

void PipeWireRegistry::OnNodeParam(void* data, int, uint32_t id, uint32_t, uint32_t,
                                   const spa_pod* param) {
    auto* bound = static_cast<Bound*>(data);
    if (id != SPA_PARAM_Props || param == nullptr || !spa_pod_is_object(param)) return;
    const spa_pod_prop* prop = spa_pod_find_prop(param, nullptr, SPA_PROP_channelVolumes);
    if (prop == nullptr) return;

    float volumes[SPA_AUDIO_MAX_CHANNELS];
    uint32_t n =
        spa_pod_copy_array(&prop->value, SPA_TYPE_Float, volumes, SPA_AUDIO_MAX_CHANNELS);
    if (n == 0) return;

    auto* self = bound->self;
    auto it = self->nodes_.find(bound->id);
    if (it == self->nodes_.end()) return;
    std::vector<float> updated(volumes, volumes + n);
    if (updated == it->second.channel_volumes) return;
    it->second.channel_volumes = std::move(updated);
    if (self->callbacks_.volume_changed) self->callbacks_.volume_changed(bound->id);
}

void PipeWireRegistry::OnDeviceInfo(void* data, const pw_device_info* info) {
    auto* bound = static_cast<Bound*>(data);
    auto* self = bound->self;
    if ((info->change_mask & PW_DEVICE_CHANGE_MASK_PROPS) == 0) return;
    auto it = self->devices_.find(bound->id);
    if (it == self->devices_.end()) return;
    Properties props = ToProperties(info->props);
    if (props == it->second.props) return;
    it->second.props = std::move(props);
    self->NotifyChanged();
}

const PwNode* PipeWireRegistry::FindNode(uint32_t id) const {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? &it->second : nullptr;
}

const PwNode* PipeWireRegistry::FindNodeByName(const std::string& name) const {
    for (const auto& [id, node] : nodes_) {
        if (node.Name() == name) return &node;
    }
    return nullptr;
}

const PwDevice* PipeWireRegistry::FindDevice(uint32_t id) const {
    auto it = devices_.find(id);
    return it != devices_.end() ? &it->second : nullptr;
}

bool PipeWireRegistry::SetNodeVolume(uint32_t node_id, float linear) {
    auto bound = bound_.find(node_id);
    auto node = nodes_.find(node_id);
    if (bound == bound_.end() || node == nodes_.end()) return false;

    uint32_t channels = static_cast<uint32_t>(node->second.channel_volumes.size());
    if (channels == 0) channels = 2;  // our loopbacks are created with FL FR
    float volumes[SPA_AUDIO_MAX_CHANNELS];
    for (uint32_t i = 0; i < channels; ++i) volumes[i] = linear;

    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
    spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, channels, volumes);
    auto* pod = static_cast<spa_pod*>(spa_pod_builder_pop(&b, &f));

    int r = pw_node_set_param(reinterpret_cast<pw_node*>(bound->second->proxy),
                              SPA_PARAM_Props, 0, pod);
    if (r < 0) {
        PRIOMIX_LOG_WARN("Setting volume on node %u failed: %s", node_id,
                         spa_strerror(r));
        return false;
    }
    return true;
}

bool PipeWireRegistry::HasDefaultMetadata() const {
    return metadata_id_.has_value();
}

bool PipeWireRegistry::SetMetadata(uint32_t subject, const char* key, const char* type,
                                   const char* value) {
    if (!metadata_id_) return false;
    auto it = bound_.find(*metadata_id_);
    if (it == bound_.end()) return false;
    int r = pw_metadata_set_property(reinterpret_cast<pw_metadata*>(it->second->proxy),
                                     subject, key, type, value);
    if (r < 0) {
        PRIOMIX_LOG_WARN("Setting metadata %s on %u failed: %s", key, subject,
                         spa_strerror(r));
        return false;
    }
    return true;
}

pw_impl_module* PipeWireRegistry::LoadModule(const char* name, const std::string& args) {
    pw_impl_module* module =
        pw_context_load_module(context_, name, args.c_str(), nullptr);
    if (module == nullptr) {
        PRIOMIX_LOG_ERROR("Failed to load %s: %s", name, std::strerror(errno));
    }
    return module;
}

void PipeWireRegistry::UnloadModule(pw_impl_module* module) {
    if (module != nullptr) pw_impl_module_destroy(module);
}

}  // namespace priomix
