// SPDX-License-Identifier: GPL-3.0-only
#include "pw_graph.hpp"

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace priomix::test {

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kChannels = 2;

// RAII lock of the thread loop.
class LoopLock {
public:
    explicit LoopLock(pw_thread_loop* loop) : loop_(loop) { pw_thread_loop_lock(loop_); }
    ~LoopLock() { pw_thread_loop_unlock(loop_); }

private:
    pw_thread_loop* loop_;
};

}  // namespace

struct PwGraph::Hooks {
    spa_hook core_listener{};
    spa_hook registry_listener{};
    spa_hook metadata_listener{};
    pw_core_events core_events{};
    pw_registry_events registry_events{};
    pw_metadata_events metadata_events{};
    int pending_seq = -1;
    bool done = false;
    PwGraph* self = nullptr;
};

struct PwGraph::Stream {
    pw_stream* stream = nullptr;
    spa_hook listener{};
    pw_stream_events events{};
    bool capture = false;
    float amplitude = 0.5f;
    double phase = 0;
    std::mutex mutex;
    double sum = 0;
    uint64_t count = 0;

    // Destroyed with the loop lock held.
    ~Stream() {
        if (stream != nullptr) pw_stream_destroy(stream);
    }

    static void OnProcess(void* data) {
        auto* self = static_cast<Stream*>(data);
        pw_buffer* b = pw_stream_dequeue_buffer(self->stream);
        if (b == nullptr) return;
        spa_data& d = b->buffer->datas[0];
        if (d.data == nullptr) {
            pw_stream_queue_buffer(self->stream, b);
            return;
        }
        if (self->capture) {
            uint32_t size = std::min(d.chunk->size, d.maxsize);
            auto* samples = SPA_PTROFF(d.data, d.chunk->offset, float);
            uint32_t n = size / sizeof(float);
            double sum = 0;
            for (uint32_t i = 0; i < n; ++i) sum += double(samples[i]) * samples[i];
            std::lock_guard lock(self->mutex);
            self->sum += sum;
            self->count += n;
        } else {
            const uint32_t stride = sizeof(float) * kChannels;
            uint32_t frames = d.maxsize / stride;
            if (b->requested != 0) frames = std::min<uint32_t>(frames, b->requested);
            auto* out = static_cast<float*>(d.data);
            for (uint32_t i = 0; i < frames; ++i) {
                float v = self->amplitude * static_cast<float>(std::sin(self->phase));
                self->phase += 2 * M_PI * 440.0 / kRate;
                if (self->phase > 2 * M_PI) self->phase -= 2 * M_PI;
                for (uint32_t c = 0; c < kChannels; ++c) out[i * kChannels + c] = v;
            }
            d.chunk->offset = 0;
            d.chunk->stride = stride;
            d.chunk->size = frames * stride;
        }
        pw_stream_queue_buffer(self->stream, b);
    }
};

PwGraph::PwGraph() : hooks_(std::make_unique<Hooks>()) {
    pw_init(nullptr, nullptr);
    hooks_->self = this;
}

PwGraph::~PwGraph() {
    if (loop_ != nullptr) {
        pw_thread_loop_lock(loop_);
        tones_.clear();
        meters_.clear();
        for (auto& [name, proxy] : devices_) pw_proxy_destroy(proxy);
        devices_.clear();
        if (metadata_ != nullptr) {
            spa_hook_remove(&hooks_->metadata_listener);
            pw_proxy_destroy(metadata_);
        }
        if (registry_ != nullptr) {
            spa_hook_remove(&hooks_->registry_listener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry_));
        }
        if (core_ != nullptr) {
            spa_hook_remove(&hooks_->core_listener);
            pw_core_disconnect(core_);
        }
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_stop(loop_);
        if (context_ != nullptr) pw_context_destroy(context_);
        pw_thread_loop_destroy(loop_);
    }
    pw_deinit();
}

bool PwGraph::Connect() {
    loop_ = pw_thread_loop_new("priomix-test", nullptr);
    if (loop_ == nullptr) return false;
    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (context_ == nullptr || pw_thread_loop_start(loop_) < 0) return false;

    LoopLock lock(loop_);
    core_ = pw_context_connect(context_, nullptr, 0);
    if (core_ == nullptr) return false;

    hooks_->core_events.version = PW_VERSION_CORE_EVENTS;
    hooks_->core_events.done = [](void* data, uint32_t id, int seq) {
        auto* hooks = static_cast<Hooks*>(data);
        if (id == PW_ID_CORE && seq == hooks->pending_seq) {
            hooks->done = true;
            pw_thread_loop_signal(hooks->self->loop_, false);
        }
    };
    pw_core_add_listener(core_, &hooks_->core_listener, &hooks_->core_events,
                         hooks_.get());

    hooks_->registry_events.version = PW_VERSION_REGISTRY_EVENTS;
    hooks_->registry_events.global = &PwGraph::OnGlobal;
    hooks_->registry_events.global_remove = &PwGraph::OnGlobalRemove;
    registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(registry_, &hooks_->registry_listener,
                             &hooks_->registry_events, this);
    return Sync();
}

bool PwGraph::Sync() {
    // Called with the loop lock held.
    hooks_->done = false;
    hooks_->pending_seq = pw_core_sync(core_, PW_ID_CORE, 0);
    while (!hooks_->done) {
        if (pw_thread_loop_timed_wait(loop_, 5) != 0) return false;
    }
    return true;
}

void PwGraph::OnGlobal(void* data, uint32_t id, uint32_t, const char* type, uint32_t,
                       const spa_dict* props) {
    auto* self = static_cast<PwGraph*>(data);
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0 && props != nullptr) {
        Node node;
        node.id = id;
        const char* name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        const char* serial = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL);
        node.name = name != nullptr ? name : "";
        node.serial = serial != nullptr ? std::strtoull(serial, nullptr, 10) : 0;
        self->nodes_[id] = node;
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 && props != nullptr &&
               self->metadata_ == nullptr) {
        const char* name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
        if (name == nullptr || std::strcmp(name, "default") != 0) return;
        self->metadata_ = static_cast<pw_proxy*>(pw_registry_bind(
            self->registry_, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
        self->hooks_->metadata_events.version = PW_VERSION_METADATA_EVENTS;
        self->hooks_->metadata_events.property = &PwGraph::OnMetadataProperty;
        pw_metadata_add_listener(reinterpret_cast<pw_metadata*>(self->metadata_),
                                 &self->hooks_->metadata_listener,
                                 &self->hooks_->metadata_events, self);
    }
}

void PwGraph::OnGlobalRemove(void* data, uint32_t id) {
    static_cast<PwGraph*>(data)->nodes_.erase(id);
}

int PwGraph::OnMetadataProperty(void* data, uint32_t subject, const char* key,
                                const char*, const char* value) {
    auto* self = static_cast<PwGraph*>(data);
    if (key == nullptr) {  // all keys of the subject removed
        std::erase_if(self->md_,
                      [subject](const auto& e) { return e.first.first == subject; });
    } else if (value == nullptr) {
        self->md_.erase({subject, key});
    } else {
        self->md_[{subject, key}] = value;
    }
    return 0;
}

bool PwGraph::AddDevice(const std::string& name, const std::string& media_class) {
    LoopLock lock(loop_);
    pw_properties* props = pw_properties_new(
        "factory.name", "support.null-audio-sink", PW_KEY_NODE_NAME, name.c_str(),
        PW_KEY_NODE_DESCRIPTION, ("Test " + name).c_str(), PW_KEY_MEDIA_CLASS,
        media_class.c_str(), "audio.position", "FL,FR", nullptr);
    auto* proxy = static_cast<pw_proxy*>(pw_core_create_object(
        core_, "adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &props->dict, 0));
    pw_properties_free(props);
    if (proxy == nullptr) return false;
    devices_[name] = proxy;
    return Sync();
}

void PwGraph::RemoveDevice(const std::string& name) {
    LoopLock lock(loop_);
    auto it = devices_.find(name);
    if (it == devices_.end()) return;
    pw_proxy_destroy(it->second);
    devices_.erase(it);
    Sync();
}

std::optional<uint32_t> PwGraph::NodeId(const std::string& name) {
    LoopLock lock(loop_);
    for (const auto& [id, node] : nodes_) {
        if (node.name == name) return id;
    }
    return std::nullopt;
}

std::optional<uint64_t> PwGraph::NodeSerial(const std::string& name) {
    LoopLock lock(loop_);
    for (const auto& [id, node] : nodes_) {
        if (node.name == name) return node.serial;
    }
    return std::nullopt;
}

std::optional<std::string> PwGraph::Metadata(const std::string& key, uint32_t subject) {
    LoopLock lock(loop_);
    auto it = md_.find({subject, key});
    if (it == md_.end()) return std::nullopt;
    return it->second;
}

bool PwGraph::SetNodeVolume(const std::string& name, float linear) {
    auto id = NodeId(name);
    if (!id) return false;
    LoopLock lock(loop_);
    auto* node = static_cast<pw_node*>(
        pw_registry_bind(registry_, *id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
    if (node == nullptr) return false;
    float volumes[kChannels] = {linear, linear};
    uint8_t buffer[512];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
    spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, kChannels, volumes);
    auto* pod = static_cast<spa_pod*>(spa_pod_builder_pop(&b, &f));
    pw_node_set_param(node, SPA_PARAM_Props, 0, pod);
    bool ok = Sync();
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    return ok;
}

std::optional<float> PwGraph::NodeVolume(const std::string& name) {
    auto id = NodeId(name);
    if (!id) return std::nullopt;
    LoopLock lock(loop_);
    auto* node = static_cast<pw_node*>(
        pw_registry_bind(registry_, *id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
    if (node == nullptr) return std::nullopt;

    struct Query {
        std::optional<float> volume;
        spa_hook listener{};
        pw_node_events events{};
    } query;
    query.events.version = PW_VERSION_NODE_EVENTS;
    query.events.param = [](void* data, int, uint32_t id, uint32_t, uint32_t,
                            const spa_pod* param) {
        auto* q = static_cast<Query*>(data);
        if (id != SPA_PARAM_Props || param == nullptr) return;
        const spa_pod_prop* prop =
            spa_pod_find_prop(param, nullptr, SPA_PROP_channelVolumes);
        float volumes[SPA_AUDIO_MAX_CHANNELS];
        if (prop != nullptr && spa_pod_copy_array(&prop->value, SPA_TYPE_Float, volumes,
                                                  SPA_AUDIO_MAX_CHANNELS) > 0) {
            q->volume = volumes[0];
        }
    };
    pw_node_add_listener(node, &query.listener, &query.events, &query);
    pw_node_enum_params(node, 0, SPA_PARAM_Props, 0, 1, nullptr);
    Sync();
    spa_hook_remove(&query.listener);
    pw_proxy_destroy(reinterpret_cast<pw_proxy*>(node));
    return query.volume;
}

std::unique_ptr<PwGraph::Stream> PwGraph::CreateStream(const std::string& target,
                                                       bool capture, float amplitude) {
    auto stream = std::make_unique<Stream>();
    stream->capture = capture;
    stream->amplitude = amplitude;
    pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
        capture ? "Capture" : "Playback", PW_KEY_TARGET_OBJECT, target.c_str(),
        "state.restore-props", "false", "state.restore-target", "false", nullptr);
    if (capture) pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
    std::string name = std::string(capture ? "meter-" : "tone-") + target;

    stream->stream = pw_stream_new(core_, name.c_str(), props);
    if (stream->stream == nullptr) return nullptr;
    stream->events.version = PW_VERSION_STREAM_EVENTS;
    stream->events.process = &Stream::OnProcess;
    pw_stream_add_listener(stream->stream, &stream->listener, &stream->events,
                           stream.get());

    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = kRate;
    info.channels = kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod* params[1] = {
        spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};

    int r = pw_stream_connect(
        stream->stream, capture ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                     PW_STREAM_FLAG_MAP_BUFFERS |
                                     PW_STREAM_FLAG_RT_PROCESS),
        params, 1);
    if (r < 0) return nullptr;
    return stream;
}

bool PwGraph::StartTone(const std::string& sink, float amplitude) {
    LoopLock lock(loop_);
    auto stream = CreateStream(sink, false, amplitude);
    if (!stream) return false;
    tones_[sink] = std::move(stream);
    return true;
}

void PwGraph::StopTones() {
    LoopLock lock(loop_);
    tones_.clear();
}

bool PwGraph::StartMeter(const std::string& sink) {
    LoopLock lock(loop_);
    auto stream = CreateStream(sink, true, 0);
    if (!stream) return false;
    meters_[sink] = std::move(stream);
    return true;
}

double PwGraph::MeasureRms(const std::string& sink, int ms) {
    Stream* stream = nullptr;
    {
        LoopLock lock(loop_);
        auto it = meters_.find(sink);
        if (it == meters_.end()) return -1;
        stream = it->second.get();
    }
    {
        std::lock_guard lock(stream->mutex);
        stream->sum = 0;
        stream->count = 0;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    std::lock_guard lock(stream->mutex);
    return stream->count > 0 ? std::sqrt(stream->sum / double(stream->count)) : 0.0;
}

}  // namespace priomix::test
