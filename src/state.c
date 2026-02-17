#include "state.h"
#include <stdio.h>
#include <string.h>

MixerState shared_state;

void state_init(void)
{
    pthread_mutex_init(&shared_state.lock, NULL);
    for (int i = 0; i < MAX_NODES; i++) {
        shared_state.nodes[i].active = false;
    }
}

void state_add_node(uint32_t id, const char *name, const char *class)
{
    pthread_mutex_lock(&shared_state.lock);

    for (int i = 0; i < MAX_NODES; i++) {
        if (!shared_state.nodes[i].active) {
            shared_state.nodes[i].id = id;
            shared_state.nodes[i].active = true;
            atomic_store(&shared_state.nodes[i].peak, 0.0f);
            atomic_store(&shared_state.nodes[i].volume, 1.0f);

            if (name)
                strncpy(shared_state.nodes[i].name, name, 128);
            if (class)
                strncpy(shared_state.nodes[i].class, class, 64);

            printf("Node added to state: %s (ID: %u)\n", name, id);
            break;
        }
    }

    pthread_mutex_unlock(&shared_state.lock);
}

void state_remove_node(uint32_t id)
{
    pthread_mutex_lock(&shared_state.lock);

    for (int i = 0; i < MAX_NODES; i++) {
        if (shared_state.nodes[i].active && shared_state.nodes[i].id == id) {
            shared_state.nodes[i].active = false;
            printf("Node removed from state: ID %u\n", id);
            break;
        }
    }

    pthread_mutex_unlock(&shared_state.lock);
}

/* use atomics for high-frequency updates */
void state_update_peak(uint32_t id, float peak_value)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (shared_state.nodes[i].active && shared_state.nodes[i].id == id) {
            atomic_store(&shared_state.nodes[i].peak, peak_value);
            break;
        }
    }
}
