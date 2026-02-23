#include "state.h"
#include "common.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zmq.h>

MixerState shared_state;

uint8_t state_init(void)
{
    pthread_mutex_init(&shared_state.lock, NULL);
    shared_state.zmq_ctx = zmq_ctx_new();
    if (!shared_state.zmq_ctx) {
        return FAILURE;
    }

    atomic_store(&shared_state.is_ready, false);

    for (int i = 0; i < MAX_NODES; i++) {
        shared_state.nodes[i].active = false;
    }

    return SUCCESS;
}

uint8_t state_add_node(uint32_t id, const char *name, const char *class)
{
    pthread_mutex_lock(&shared_state.lock);

    int result = FAILURE;

    for (int i = 0; i < MAX_NODES; i++) {
        if (!shared_state.nodes[i].active) {
            shared_state.nodes[i].id = id;
            shared_state.nodes[i].active = true;
            atomic_store(&shared_state.nodes[i].peak, 0.0f);
            atomic_store(&shared_state.nodes[i].volume, 1.0f);

            if (name) {
                strncpy(shared_state.nodes[i].name, name, 128);
                shared_state.nodes[i].name[127] = '\0';
            }

            if (class) {
                strncpy(shared_state.nodes[i].class, class, 64);
                shared_state.nodes[i].class[63] = '\0';
            }

            result = SUCCESS;
            break;
        }
    }

    if (!result) {
        printf("Node added to state: %s (ID: %u)\n", name, id);
    } else {
        printf("Unable to add node\n");
    }

    pthread_mutex_unlock(&shared_state.lock);

    return result;
}

// this function IS NOT thread safe, only use inside lock
int32_t get_node_index(uint32_t id)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (shared_state.nodes[i].active && shared_state.nodes[i].id == id) {
            return i;
        }
    }

    return -1;
}

int32_t get_node_index_thread_safe(uint32_t id)
{
    pthread_mutex_lock(&shared_state.lock);

    int32_t result = -1;
    for (int i = 0; i < MAX_NODES; i++) {
        if (shared_state.nodes[i].active && shared_state.nodes[i].id == id) {
            result = i;
            goto cleanup;
        }
    }

cleanup:
    pthread_mutex_unlock(&shared_state.lock);
    return result;
}

uint8_t state_remove_node(uint32_t id)
{
    pthread_mutex_lock(&shared_state.lock);

    uint8_t result = SUCCESS;

    int32_t index = get_node_index(id);
    if (index < 0) {
        result = FAILURE;
        goto cleanup;
    }

    shared_state.nodes[index].active = false;
    printf("Node removed from state: ID %u\n", id);

cleanup:
    pthread_mutex_unlock(&shared_state.lock);
    return result;
}

uint8_t state_update_name(uint32_t id, const char *name)
{
    uint8_t result = SUCCESS;

    if (!name) {
        result = FAILURE;
        goto cleanup;
    }

    pthread_mutex_lock(&shared_state.lock);
    int32_t index = get_node_index(id);

    strncpy(shared_state.nodes[index].name, name, 128);
    shared_state.nodes[index].name[127] = '\0';

    if (index < 0) {
        result = FAILURE;
        goto cleanup;
    }

cleanup:
    pthread_mutex_unlock(&shared_state.lock);
    return result;
}

/* use atomics for high-frequency updates */
uint8_t state_update_peak(uint32_t id, float peak_value)
{
    pthread_mutex_lock(&shared_state.lock);
    int32_t index = get_node_index(id);

    uint8_t result = SUCCESS;
    if (index < 0) {
        result = FAILURE;
        goto cleanup;
    }

    atomic_store(&shared_state.nodes[index].peak, peak_value);

cleanup:
    pthread_mutex_unlock(&shared_state.lock);
    return result;
}
