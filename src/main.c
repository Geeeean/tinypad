/*
  [title]
  \ref page_tutorial3
  [title]
 */
/* [code] */
#include "spa/utils/dict.h"
#include <pipewire/pipewire.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define MAX_NODES 64

struct AudioNode {
    uint32_t id;
    struct pw_proxy *proxy;
    struct spa_hook listener;
    char name[128];
    char class[64];

    /* Using atomic for the volume and peaks so Raylib can read
       them without locking the mutex every frame */
    _Atomic float volume;
    _Atomic float peak;

    bool active;
};

/* This is your 'Central Database' */
struct MixerState {
    struct AudioNode nodes[MAX_NODES];
    pthread_mutex_t lock; /* Protects the structure of the list (add/remove) */
};

/* Global instance or passed via user_data */
struct MixerState state;

/* [roundtrip] */
struct roundtrip_data {
    int pending;
    struct pw_main_loop *loop;
};

static void on_core_done(void *data, uint32_t id, int seq)
{
    struct roundtrip_data *d = data;

    if (id == PW_ID_CORE && seq == d->pending) {
        printf("ROUNDTRIP END\n");
    }
}

static void roundtrip(struct pw_core *core, struct pw_main_loop *loop)
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

    if ((err = pw_main_loop_run(loop)) < 0)
        printf("main_loop_run error:%d!\n", err);

    spa_hook_remove(&core_listener);
}
/* [roundtrip] */

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

    if (strcmp(class, "Stream/Output/Audio") == 0 || strcmp(class, "Audio/Sink") == 0) {
        printf("object: id:%u type:%s/%d, name: %s, desc: %s\n", id, type, version,
               spa_dict_lookup(props, "node.name"),
               spa_dict_lookup(props, "node.description"));
    }
}

static void registry_event_global_remove(void *data, uint32_t id)
{
    /* Search for the node with this ID in your array/list.
       Once found, destroy the proxy and clear the slot.
    */
    printf("Object %u removed from the server\n", id);
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    .global_remove = registry_event_global_remove,
};

static void node_event_info(void *data, const struct pw_node_info *info)
{
    struct AudioNode *node = data;

    if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS) {
        printf("Received update for: %s\n", node->name);
    }
}

static const struct pw_node_events node_events = {
    PW_VERSION_NODE_EVENTS,
    .info = node_event_info,
};

int main(int argc, char *argv[])
{
    struct pw_main_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    struct spa_hook registry_listener;

    pw_init(&argc, &argv);

    loop = pw_main_loop_new(NULL /* properties */);
    context = pw_context_new(pw_main_loop_get_loop(loop), NULL /* properties */,
                             0 /* user_data size */);

    core = pw_context_connect(context, NULL /* properties */, 0 /* user_data size */);

    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0 /* user_data size */);

    pw_registry_add_listener(registry, &registry_listener, &registry_events, NULL);

    roundtrip(core, loop);

    pw_proxy_destroy((struct pw_proxy *)registry);
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);

    return 0;
}
/* [code] */
