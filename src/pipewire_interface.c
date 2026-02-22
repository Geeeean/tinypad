#include "pipewire_interface.h"
#include "command.h"
#include "spa/pod/pod.h"
#include "spa/utils/dict.h"
#include "state.h"
#include <math.h>
#include <pipewire/pipewire.h>
#include <pthread.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <stdatomic.h>
#include <stdio.h>

struct pw_context_data {
    struct pw_core *core;
    struct pw_registry *registry;
};

static void on_process(void *data)
{
    AudioNode *node = data;
    struct pw_buffer *buf = pw_stream_dequeue_buffer(node->meter_stream);
    if (!buf)
        return;

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
        if (s > peak)
            peak = s;
    }

    node->peak_accum = fmaxf(node->peak_accum, peak);
    node->peak_counter++;

    if (node->peak_counter >= 10) {
        atomic_store(&node->peak, node->peak_accum);
        printf("PEAK %f\n", peak);
        node->peak_accum = 0.0f;
        node->peak_counter = 0;
    }

    pw_stream_queue_buffer(node->meter_stream, buf);
}

static const struct pw_stream_events meter_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

struct roundtrip_data {
    int pending;
    struct pw_thread_loop *loop;
};

void apply_node_volume(struct pw_node *proxy, float volume)
{
    if (!proxy) {
        return;
    }

    uint8_t buffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_pod_frame f;

    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);

    spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
    float vol = powf(volume, 3.0);
    spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, 1, &vol);

    spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
    spa_pod_builder_bool(&b, false);

    struct spa_pod *param = spa_pod_builder_pop(&b, &f);
    pw_node_set_param(proxy, SPA_PARAM_Props, 0, param);
}

static void on_core_done(void *data, uint32_t id, int seq)
{
    struct roundtrip_data *d = data;

    if (id == PW_ID_CORE && seq == d->pending) {
        // pw_main_loop_quit(d->loop);
    }
}

static void roundtrip(struct pw_core *core, struct pw_thread_loop *loop)
{
    static const struct pw_core_events core_events = {
        PW_VERSION_CORE_EVENTS,
        .done = on_core_done,
    };

    struct roundtrip_data d = {.loop = loop};
    struct spa_hook core_listener;
    int err;

    pw_core_add_listener(core, &core_listener, &core_events, &d);

    d.pending = pw_core_sync(core, PW_ID_CORE, 0);

    if ((err = pw_thread_loop_start(loop)) < 0) {
        // todo log error
    }

    spa_hook_remove(&core_listener);
}

static void node_event_info(void *data, const struct pw_node_info *info)
{
    AudioNode *node = (AudioNode *)data;
    if (!node) {
        // todo log error
        return;
    }

    if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS && info->props) {
        const char *desc = spa_dict_lookup(info->props, PW_KEY_NODE_DESCRIPTION);
        if (!desc)
            desc = spa_dict_lookup(info->props, PW_KEY_NODE_NICK);
        if (!desc)
            desc = spa_dict_lookup(info->props, PW_KEY_MEDIA_NAME);
        if (!desc)
            desc = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);

        if (desc) {
            state_update_name(node->id, desc);
        } else {
            // todo log error
        }
    }

    for (uint32_t i = 0; i < info->n_params; i++) {
        if (info->params[i].id == SPA_PARAM_Props) {
            pw_node_enum_params((struct pw_node *)node->proxy, 0, SPA_PARAM_Props, 0, 0,
                                NULL);
        }
    }
}

static void node_event_param(void *data, int seq, uint32_t id, uint32_t index,
                             uint32_t next, const struct spa_pod *param)
{
    AudioNode *node = data;

    if (id != SPA_PARAM_Props)
        return;

    float volumes[SPA_AUDIO_MAX_CHANNELS];
    uint32_t n_volumes = 0;
    bool mute = false;

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
            struct spa_pod_array_body *body;
            if (SPA_POD_TYPE(pod) == SPA_TYPE_Array) {
                body = (struct spa_pod_array_body *)SPA_POD_BODY(pod);
                if (body->child.type == SPA_TYPE_Float) {
                    n_volumes =
                        (SPA_POD_BODY_SIZE(pod) - sizeof(struct spa_pod_array_body)) /
                        sizeof(float);
                    if (n_volumes > SPA_AUDIO_MAX_CHANNELS)
                        n_volumes = SPA_AUDIO_MAX_CHANNELS;
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
    if (n_volumes == 0)
        return;

    /* Average the channel volumes, or just use volumes[0] */
    float avg = 0.0f;
    for (uint32_t i = 0; i < n_volumes; i++)
        avg += volumes[i];
    avg /= n_volumes;

    atomic_store(&node->volume, mute ? 0.0f : powf(avg, 0.33));
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
    if (strcmp(type, PW_TYPE_INTERFACE_Node) != 0) {
        return;
    }

    if (!props) {
        return;
    }

    const char *class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!class) {
        return;
    }

    struct pw_context_data *ctx = data;
    struct pw_registry *registry = ctx->registry;

    if (strcmp(class, "Stream/Output/Audio") == 0 || strcmp(class, "Audio/Sink") == 0) {
        const char *display_name = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
        if (display_name == NULL) {
            display_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        }

        if (display_name == NULL) {
            display_name = "Unknown Node";
        }

        state_add_node(id, display_name, class);

        struct pw_node *proxy = pw_registry_bind(registry, id, type, PW_VERSION_NODE, 0);

        if (proxy) {
            /* 3. Find the index we just used to store the proxy and add listener */
            pthread_mutex_lock(&shared_state.lock);
            int32_t idx = get_node_index(id);

            if (idx != -1) {
                shared_state.nodes[idx].proxy = (struct pw_proxy *)proxy;

                /* 4. Add the listener to this specific node.
                   We pass '&shared_state.nodes[idx]' as 'data'
                   so the callback knows which node is talking */
                pw_node_add_listener(proxy, &shared_state.nodes[idx].listener,
                                     &node_events, &shared_state.nodes[idx]);

                uint8_t buffer[1024];
                struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
                const struct spa_pod *params[1];
                params[0] = spa_format_audio_raw_build(
                    &b, SPA_PARAM_EnumFormat,
                    &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32));

                struct pw_stream *meter_stream = pw_stream_new(
                    ctx->core, "peak_meter",
                    pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                                      "Capture", PW_KEY_MEDIA_ROLE, "DSP", NULL));

                pw_stream_add_listener(meter_stream,
                                       &shared_state.nodes[idx].meter_listener,
                                       &meter_events, &shared_state.nodes[idx]);

                pw_stream_connect(meter_stream, PW_DIRECTION_INPUT, id,
                                  PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_RT_PROCESS,
                                  params, 1);

                shared_state.nodes[idx].meter_stream = meter_stream;
            }
            pthread_mutex_unlock(&shared_state.lock);
        }
    }
}

static void registry_event_global_remove(void *data, uint32_t id)
{
    state_remove_node(id);
    // todo: destroy proxy and listener
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    .global_remove = registry_event_global_remove,
};

void *pipewire_interface_loop(void *args)
{
    ZmqConnection zmq = command_connection_init_audio();
    if (!zmq.is_active) {
        return NULL;
    }

    struct pw_thread_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    struct spa_hook registry_listener;

    pw_init(NULL, NULL);

    loop = pw_thread_loop_new("velvet_pipewire_loop", NULL /* properties */);
    context = pw_context_new(pw_thread_loop_get_loop(loop), NULL /* properties */,
                             0 /* user_data size */);

    core = pw_context_connect(context, NULL /* properties */, 0 /* user_data size */);

    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0 /* user_data size */);

    struct pw_context_data ctx = {.registry = registry, .core = core};
    pw_registry_add_listener(registry, &registry_listener, &registry_events, &ctx);

    roundtrip(core, loop);

    Command cmd;
    while (true) {
        if (command_recv_blocking(zmq, &cmd) > 0) {
            pw_thread_loop_lock(loop);

            struct pw_node *target_proxy = NULL;
            for (int i = 0; i < MAX_NODES; i++) {
                if (shared_state.nodes[i].active &&
                    shared_state.nodes[i].id == cmd.node_id) {
                    target_proxy = (struct pw_node *)shared_state.nodes[i].proxy;
                    break;
                }
            }

            if (cmd.type == CMD_SET_VOLUME) {
                apply_node_volume(target_proxy, cmd.value);
            }

            pw_thread_loop_unlock(loop);
        }
    }

    pw_proxy_destroy((struct pw_proxy *)registry);
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_thread_loop_destroy(loop);

    return NULL;
}

void pipewire_loop_spawn()
{
    pthread_t pipewire_t;

    // Creating a new thread
    pthread_create(&pipewire_t, NULL, pipewire_interface_loop, NULL);
}
