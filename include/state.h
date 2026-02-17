#ifndef STATE_H
#define STATE_H

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

    /* lock-free updates of audio levels */
    _Atomic float volume;
    _Atomic float peak;

    bool active;
    struct pw_proxy *proxy;
    struct spa_hook listener;
} AudioNode;

typedef struct {
    AudioNode nodes[MAX_NODES];
    pthread_mutex_t lock;
} MixerState;

/* Global state instance */
extern MixerState shared_state;

void state_init(void);
void state_add_node(uint32_t id, const char *name, const char *class);
void state_remove_node(uint32_t id);
void state_update_peak(uint32_t id, float peak_value);

#endif
