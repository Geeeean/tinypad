// PipeWire audio_backend implementation. Ported from the previous
// pipewire_interface.c/state.c pair: same node enumeration, volume control
// (cubic taper) and peak-metering approach, restructured behind the
// audio_backend_vtable_t interface and reporting sessions as value snapshots
// instead of owning shared global state.
//
// PipeWire already drives everything from its own thread (pw_thread_loop,
// started in create()), so poll() is a no-op -- on_added/on_updated/
// on_removed fire directly from PipeWire's callbacks, on that thread.

#include "platform/audio_backend.h"
#include <math.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/utils/dict.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BACKEND_NODES 64

typedef struct {
    uint32_t id;
    bool active;
    char name[AUDIO_SESSION_NAME_LEN];
    _Atomic float volume;
    _Atomic float peak;
    bool muted;

    float peak_accum;
    uint32_t peak_counter;

    struct pw_proxy *proxy; // actually a struct pw_node *
    struct spa_hook listener;
    struct spa_hook meter_listener;
    struct pw_stream *meter_stream;

    audio_backend_t *backend; // back-pointer so node/stream callbacks can fire on_updated
} backend_node_t;

typedef struct {
    audio_backend_t *backend;
    struct pw_registry *registry;
} registry_ctx_t;

struct audio_backend {
    struct pw_thread_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    struct spa_hook registry_listener;
    registry_ctx_t registry_ctx;

    backend_node_t nodes[MAX_BACKEND_NODES];

    audio_session_cb on_added;
    audio_session_cb on_updated;
    audio_session_cb on_removed;
    void *user_data;
};

static void build_session_snapshot(backend_node_t *node, audio_session_t *out)
{
    out->id = node->id;
    snprintf(out->name, sizeof(out->name), "%s", node->name);
    out->volume = atomic_load(&node->volume);
    out->peak = atomic_load(&node->peak);
    out->muted = node->muted;
}

static backend_node_t *find_node(audio_backend_t *backend, uint32_t id)
{
    for (int i = 0; i < MAX_BACKEND_NODES; i++) {
        if (backend->nodes[i].active && backend->nodes[i].id == id) {
            return &backend->nodes[i];
        }
    }
    return NULL;
}

static backend_node_t *alloc_node(audio_backend_t *backend)
{
    for (int i = 0; i < MAX_BACKEND_NODES; i++) {
        if (!backend->nodes[i].active) {
            return &backend->nodes[i];
        }
    }
    return NULL;
}

static void apply_node_volume(struct pw_proxy *proxy, float volume)
{
    if (!proxy) {
        return;
    }

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_pod_frame f;

    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);

    spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
    // Perceptual (cubic) taper so the fader feels linear; inverted with
    // powf(x, 0.33) when reading the value back in node_event_param.
    float vol = powf(volume, 3.0f);
    spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, 1, &vol);

    struct spa_pod *param = spa_pod_builder_pop(&b, &f);
    pw_node_set_param((struct pw_node *)proxy, SPA_PARAM_Props, 0, param);
}

static void apply_node_mute(struct pw_proxy *proxy, bool muted)
{
    if (!proxy) {
        return;
    }

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_pod_frame f;

    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
    spa_pod_builder_bool(&b, muted);
    struct spa_pod *param = spa_pod_builder_pop(&b, &f);
    pw_node_set_param((struct pw_node *)proxy, SPA_PARAM_Props, 0, param);
}

static void destroy_node(backend_node_t *node)
{
    if (node->meter_stream) {
        spa_hook_remove(&node->meter_listener);
        pw_stream_destroy(node->meter_stream);
        node->meter_stream = NULL;
    }
    if (node->proxy) {
        spa_hook_remove(&node->listener);
        pw_proxy_destroy(node->proxy);
        node->proxy = NULL;
    }
    memset(node, 0, sizeof(*node));
}

static void on_process(void *data)
{
    backend_node_t *node = data;
    struct pw_buffer *buf = pw_stream_dequeue_buffer(node->meter_stream);
    if (!buf) {
        return;
    }

    struct spa_data *d = &buf->buffer->datas[0];
    if (!d->data || d->chunk->size == 0) {
        pw_stream_queue_buffer(node->meter_stream, buf);
        return;
    }

    float *samples = d->data;
    uint32_t n_samples = d->chunk->size / sizeof(float);

    float peak = 0.0f;
    for (uint32_t i = 0; i < n_samples; i++) {
        float s = fabsf(samples[i]);
        if (s > peak) {
            peak = s;
        }
    }

    node->peak_accum = fmaxf(node->peak_accum, peak);
    node->peak_counter++;

    if (node->peak_counter >= 10) {
        atomic_store(&node->peak, node->peak_accum);
        node->peak_accum = 0.0f;
        node->peak_counter = 0;

        if (node->backend && node->backend->on_updated) {
            audio_session_t snapshot;
            build_session_snapshot(node, &snapshot);
            node->backend->on_updated(&snapshot, node->backend->user_data);
        }
    }

    pw_stream_queue_buffer(node->meter_stream, buf);
}

static const struct pw_stream_events meter_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

static void node_event_info(void *data, const struct pw_node_info *info)
{
    backend_node_t *node = data;

    if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS && info->props) {
        const char *desc = spa_dict_lookup(info->props, PW_KEY_NODE_DESCRIPTION);
        if (!desc) desc = spa_dict_lookup(info->props, PW_KEY_NODE_NICK);
        if (!desc) desc = spa_dict_lookup(info->props, PW_KEY_MEDIA_NAME);
        if (!desc) desc = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
        if (desc) {
            snprintf(node->name, sizeof(node->name), "%s", desc);
        }
    }

    for (uint32_t i = 0; i < info->n_params; i++) {
        if (info->params[i].id == SPA_PARAM_Props) {
            pw_node_enum_params((struct pw_node *)node->proxy, 0, SPA_PARAM_Props, 0, 0, NULL);
        }
    }
}

static void node_event_param(void *data, int seq, uint32_t id, uint32_t index, uint32_t next,
                             const struct spa_pod *param)
{
    (void)seq;
    (void)index;
    (void)next;
    backend_node_t *node = data;

    if (id != SPA_PARAM_Props) {
        return;
    }

    float volumes[SPA_AUDIO_MAX_CHANNELS];
    uint32_t n_volumes = 0;
    bool mute = node->muted;

    struct spa_pod_prop *prop;
    struct spa_pod_object *obj = (struct spa_pod_object *)param;

    SPA_POD_OBJECT_FOREACH(obj, prop)
    {
        switch (prop->key) {
        case SPA_PROP_mute:
            spa_pod_get_bool(&prop->value, &mute);
            break;
        case SPA_PROP_channelVolumes: {
            struct spa_pod *pod = &prop->value;
            if (SPA_POD_TYPE(pod) == SPA_TYPE_Array) {
                struct spa_pod_array_body *body = (struct spa_pod_array_body *)SPA_POD_BODY(pod);
                if (body->child.type == SPA_TYPE_Float) {
                    n_volumes =
                        (SPA_POD_BODY_SIZE(pod) - sizeof(struct spa_pod_array_body)) /
                        sizeof(float);
                    if (n_volumes > SPA_AUDIO_MAX_CHANNELS) {
                        n_volumes = SPA_AUDIO_MAX_CHANNELS;
                    }
                    memcpy(volumes, SPA_POD_BODY(pod) + sizeof(struct spa_pod_array_body),
                          n_volumes * sizeof(float));
                }
            }
            break;
        }
        default:
            break;
        }
    }

    node->muted = mute;

    if (n_volumes > 0) {
        float avg = 0.0f;
        for (uint32_t i = 0; i < n_volumes; i++) {
            avg += volumes[i];
        }
        avg /= (float)n_volumes;
        atomic_store(&node->volume, mute ? 0.0f : powf(avg, 0.33f));
    }

    if (node->backend && node->backend->on_updated) {
        audio_session_t snapshot;
        build_session_snapshot(node, &snapshot);
        node->backend->on_updated(&snapshot, node->backend->user_data);
    }
}

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .info = node_event_info,
    .param = node_event_param,
};

static void registry_event_global(void *data, uint32_t id, uint32_t permissions,
                                  const char *type, uint32_t version,
                                  const struct spa_dict *props)
{
    (void)permissions;
    (void)version;
    registry_ctx_t *ctx = data;
    audio_backend_t *backend = ctx->backend;

    if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || !props) {
        return;
    }

    const char *class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!class) {
        return;
    }
    if (strcmp(class, "Stream/Output/Audio") != 0 && strcmp(class, "Audio/Sink") != 0) {
        return;
    }

    backend_node_t *node = alloc_node(backend);
    if (!node) {
        return;
    }

    const char *display_name = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    if (!display_name) {
        display_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    }
    if (!display_name) {
        display_name = "Unknown Node";
    }

    node->id = id;
    node->active = true;
    node->backend = backend;
    snprintf(node->name, sizeof(node->name), "%s", display_name);
    atomic_store(&node->volume, 1.0f);
    atomic_store(&node->peak, 0.0f);

    audio_session_t snapshot;
    build_session_snapshot(node, &snapshot);
    if (backend->on_added) {
        backend->on_added(&snapshot, backend->user_data);
    }

    struct pw_node *proxy =
        (struct pw_node *)pw_registry_bind(ctx->registry, id, type, PW_VERSION_NODE, 0);
    if (!proxy) {
        return;
    }
    node->proxy = (struct pw_proxy *)proxy;
    pw_node_add_listener(proxy, &node->listener, &node_events, node);

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(
        &b, SPA_PARAM_EnumFormat,
        &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32, .rate = 48000, .channels = 2,
                                 .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR}));

    struct pw_stream *meter_stream = pw_stream_new(
        backend->core, "tinypad_peak_meter",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "DSP", "stream.dont-remix", "true",
                          "channelmix.upmix", "false", "channelmix.normalize", "false", NULL));
    if (!meter_stream) {
        return;
    }

    node->meter_stream = meter_stream;
    pw_stream_add_listener(meter_stream, &node->meter_listener, &meter_events, node);
    pw_stream_connect(meter_stream, PW_DIRECTION_INPUT, id,
                      PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_RT_PROCESS, params, 1);
}

static void registry_event_global_remove(void *data, uint32_t id)
{
    registry_ctx_t *ctx = data;
    audio_backend_t *backend = ctx->backend;

    backend_node_t *node = find_node(backend, id);
    if (!node) {
        return;
    }

    audio_session_t snapshot;
    build_session_snapshot(node, &snapshot);

    destroy_node(node);

    if (backend->on_removed) {
        backend->on_removed(&snapshot, backend->user_data);
    }
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    .global_remove = registry_event_global_remove,
};

static audio_backend_t *pw_backend_create(void)
{
    audio_backend_t *backend = calloc(1, sizeof(audio_backend_t));
    if (!backend) {
        return NULL;
    }

    pw_init(NULL, NULL);

    backend->loop = pw_thread_loop_new("tinypad_pipewire_loop", NULL);
    if (!backend->loop) {
        free(backend);
        return NULL;
    }

    backend->context = pw_context_new(pw_thread_loop_get_loop(backend->loop), NULL, 0);
    backend->core = pw_context_connect(backend->context, NULL, 0);
    if (!backend->core) {
        pw_thread_loop_destroy(backend->loop);
        free(backend);
        return NULL;
    }

    pw_thread_loop_start(backend->loop);

    backend->registry = pw_core_get_registry(backend->core, PW_VERSION_REGISTRY, 0);

    backend->registry_ctx.backend = backend;
    backend->registry_ctx.registry = backend->registry;

    pw_thread_loop_lock(backend->loop);
    pw_registry_add_listener(backend->registry, &backend->registry_listener, &registry_events,
                             &backend->registry_ctx);
    pw_thread_loop_unlock(backend->loop);

    return backend;
}

static void pw_backend_destroy(audio_backend_t *backend)
{
    if (!backend) {
        return;
    }

    if (backend->loop) {
        pw_thread_loop_lock(backend->loop);
        for (int i = 0; i < MAX_BACKEND_NODES; i++) {
            if (backend->nodes[i].active) {
                destroy_node(&backend->nodes[i]);
            }
        }
        if (backend->registry) {
            pw_proxy_destroy((struct pw_proxy *)backend->registry);
        }
        pw_thread_loop_unlock(backend->loop);

        pw_thread_loop_stop(backend->loop);
    }

    if (backend->core) {
        pw_core_disconnect(backend->core);
    }
    if (backend->context) {
        pw_context_destroy(backend->context);
    }
    if (backend->loop) {
        pw_thread_loop_destroy(backend->loop);
    }

    free(backend);
}

static void pw_backend_poll(audio_backend_t *backend)
{
    (void)backend; // PipeWire dispatches on its own thread; nothing to pump here.
}

static bool pw_backend_set_volume(audio_backend_t *backend, uint32_t session_id, float volume)
{
    if (!backend->loop) {
        return false;
    }

    pw_thread_loop_lock(backend->loop);
    backend_node_t *node = find_node(backend, session_id);
    bool ok = node && node->proxy;
    if (ok) {
        apply_node_volume(node->proxy, volume);
    }
    pw_thread_loop_unlock(backend->loop);
    return ok;
}

static bool pw_backend_set_muted(audio_backend_t *backend, uint32_t session_id, bool muted)
{
    if (!backend->loop) {
        return false;
    }

    pw_thread_loop_lock(backend->loop);
    backend_node_t *node = find_node(backend, session_id);
    bool ok = node && node->proxy;
    if (ok) {
        apply_node_mute(node->proxy, muted);
    }
    pw_thread_loop_unlock(backend->loop);
    return ok;
}

static void pw_backend_set_callbacks(audio_backend_t *backend, audio_session_cb on_added,
                                     audio_session_cb on_updated, audio_session_cb on_removed,
                                     void *user_data)
{
    backend->on_added = on_added;
    backend->on_updated = on_updated;
    backend->on_removed = on_removed;
    backend->user_data = user_data;
}

static const audio_backend_vtable_t vtable = {
    .create = pw_backend_create,
    .destroy = pw_backend_destroy,
    .poll = pw_backend_poll,
    .set_volume = pw_backend_set_volume,
    .set_muted = pw_backend_set_muted,
    .set_callbacks = pw_backend_set_callbacks,
};

const audio_backend_vtable_t *audio_backend_get_vtable(void) { return &vtable; }
