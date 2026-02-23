#ifndef STATE_H
#define STATE_H

#include "command.h"
#include "pipewire/stream.h"
#include "spa/utils/hook.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_NODES 64

typedef struct {
    uint32_t id;
    char name[128];
    char class[64];

    // lock-free updates of audio levels
    _Atomic float volume;
    _Atomic float peak;

    float peak_accum;
    uint32_t peak_counter;

    bool active;
    struct pw_proxy *proxy;
    struct spa_hook listener;
    struct spa_hook meter_listener;
    struct pw_stream *meter_stream;
} AudioNode;

typedef struct {
    AudioNode nodes[MAX_NODES];
    pthread_mutex_t lock;

    _Atomic bool is_ready;

    void *zmq_ctx;
} MixerState;

// global state instance
extern MixerState shared_state;

uint8_t state_init(void);
int32_t
get_node_index(uint32_t id); // this function IS NOT thread safe, only use inside lock
int32_t get_node_index_thread_safe(uint32_t id);
uint8_t state_add_node(uint32_t id, const char *name, const char *class);
uint8_t state_remove_node(uint32_t id);
uint8_t state_update_peak(uint32_t id, float peak_value);
uint8_t state_update_name(uint32_t id, const char *name);

#endif
