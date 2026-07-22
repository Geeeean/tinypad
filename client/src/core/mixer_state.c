#include "core/mixer_state.h"
#include "platform/mutex.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct mixer_state {
    const audio_backend_vtable_t *vtable;
    audio_backend_t *backend;

    mutex_t lock;
    audio_session_t sessions[MIXER_MAX_SESSIONS];
    size_t session_count;

    mixer_slot_t slots[MIXER_CHANNELS];

    // A slot with has_pending[i] set should auto-assign to the first
    // session whose name matches pending_names[i] (see
    // mixer_state_set_pending_assignment()). Same CHANNEL_NAME_LEN
    // truncation as mixer_slot_t.name -- these names round-tripped through
    // there already (profile_store persists mixer_slot_t.name, not the
    // untruncated audio_session_t.name).
    char pending_names[MIXER_CHANNELS][CHANNEL_NAME_LEN];
    bool has_pending[MIXER_CHANNELS];
};

// strncpy doesn't null-terminate when src is >= CHANNEL_NAME_LEN chars;
// mixer_slot_t.name is read elsewhere as a bounded C string (unlike the
// wire metadata_packet, which is allowed to be non-terminated / null-padded
// per the protocol contract), so guarantee termination here.
static void copy_slot_name(char dest[CHANNEL_NAME_LEN], const char *src)
{
    memset(dest, 0, CHANNEL_NAME_LEN);
    strncpy(dest, src, CHANNEL_NAME_LEN - 1);
}

static int find_session_index_locked(mixer_state_t *state, uint32_t id)
{
    for (size_t i = 0; i < state->session_count; i++) {
        if (state->sessions[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

// Refreshes any slot(s) pointing at `session` with its latest volume/peak.
static void sync_slots_locked(mixer_state_t *state, const audio_session_t *session)
{
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        mixer_slot_t *slot = &state->slots[i];
        if (!slot->assigned || slot->session_id != session->id) {
            continue;
        }
        slot->volume = (uint8_t)lroundf(session->volume * 100.0f);
        slot->peak = (uint8_t)lroundf(session->peak * 100.0f);
        slot->muted = session->muted;
        copy_slot_name(slot->name, session->name);
    }
}

// Assigns `slot` to a known session named `name` (a CHANNEL_NAME_LEN-1
// prefix match, same truncation as mixer_slot_t.name elsewhere). Caller
// must hold state->lock. Returns true and clears the slot's pending name if
// a match was found and assigned.
static bool assign_slot_by_name_locked(mixer_state_t *state, int slot, const char *name)
{
    for (size_t i = 0; i < state->session_count; i++) {
        if (strncmp(state->sessions[i].name, name, CHANNEL_NAME_LEN - 1) != 0) {
            continue;
        }
        state->slots[slot].assigned = true;
        state->slots[slot].session_id = state->sessions[i].id;
        state->slots[slot].volume = (uint8_t)lroundf(state->sessions[i].volume * 100.0f);
        state->slots[slot].peak = (uint8_t)lroundf(state->sessions[i].peak * 100.0f);
        state->slots[slot].muted = state->sessions[i].muted;
        copy_slot_name(state->slots[slot].name, state->sessions[i].name);
        state->has_pending[slot] = false;
        return true;
    }
    return false;
}

static void on_session_added(const audio_session_t *session, void *user_data)
{
    mixer_state_t *state = user_data;
    mutex_lock(&state->lock);

    if (find_session_index_locked(state, session->id) < 0 &&
        state->session_count < MIXER_MAX_SESSIONS) {
        state->sessions[state->session_count++] = *session;
    }

    for (int i = 0; i < MIXER_CHANNELS; i++) {
        if (state->has_pending[i] && !state->slots[i].assigned) {
            assign_slot_by_name_locked(state, i, state->pending_names[i]);
        }
    }

    mutex_unlock(&state->lock);
}

static void on_session_updated(const audio_session_t *session, void *user_data)
{
    mixer_state_t *state = user_data;
    mutex_lock(&state->lock);

    int idx = find_session_index_locked(state, session->id);
    if (idx >= 0) {
        state->sessions[idx] = *session;
        sync_slots_locked(state, session);
    }

    mutex_unlock(&state->lock);
}

static void on_session_removed(const audio_session_t *session, void *user_data)
{
    mixer_state_t *state = user_data;
    mutex_lock(&state->lock);

    int idx = find_session_index_locked(state, session->id);
    if (idx >= 0) {
        state->sessions[idx] = state->sessions[state->session_count - 1];
        state->session_count--;
    }

    for (int i = 0; i < MIXER_CHANNELS; i++) {
        if (state->slots[i].assigned && state->slots[i].session_id == session->id) {
            state->slots[i] = (mixer_slot_t){0};
        }
    }

    mutex_unlock(&state->lock);
}

mixer_state_t *mixer_state_create(const audio_backend_vtable_t *backend_vtable)
{
    mixer_state_t *state = calloc(1, sizeof(mixer_state_t));
    if (!state) {
        return NULL;
    }

    state->vtable = backend_vtable;
    mutex_init(&state->lock);

    state->backend = backend_vtable->create();
    if (state->backend) {
        backend_vtable->set_callbacks(state->backend, on_session_added, on_session_updated,
                                      on_session_removed, state);
    }

    return state;
}

void mixer_state_destroy(mixer_state_t *state)
{
    if (!state) {
        return;
    }
    if (state->backend) {
        state->vtable->destroy(state->backend);
    }
    mutex_destroy(&state->lock);
    free(state);
}

void mixer_state_poll(mixer_state_t *state)
{
    // Snapshot under the lock: mixer_state_set_backend() can swap these
    // pointers from another thread between mixer_state_poll() calls.
    mutex_lock(&state->lock);
    const audio_backend_vtable_t *vtable = state->vtable;
    audio_backend_t *backend = state->backend;
    mutex_unlock(&state->lock);

    if (!backend) {
        return;
    }

    vtable->poll(backend);

    if (vtable->get_master) {
        audio_session_t master;
        if (vtable->get_master(backend, &master)) {
            // Reserved sentinel, not whatever (if anything) the backend put
            // in `id` -- sync_slots_locked() only cares that this matches
            // MIXER_MASTER_SESSION_ID-bound slots.
            master.id = MIXER_MASTER_SESSION_ID;
            mutex_lock(&state->lock);
            sync_slots_locked(state, &master);
            mutex_unlock(&state->lock);
        }
    }
}

void mixer_state_set_backend(mixer_state_t *state, const audio_backend_vtable_t *backend_vtable)
{
    mutex_lock(&state->lock);
    audio_backend_t *old_backend = state->backend;
    const audio_backend_vtable_t *old_vtable = state->vtable;
    // NULL out first so a concurrent mixer_state_poll()/set_slot_volume_locked()
    // sees "no backend" instead of a pointer we're about to destroy.
    state->backend = NULL;
    state->session_count = 0;
    memset(state->slots, 0, sizeof(state->slots));
    memset(state->has_pending, 0, sizeof(state->has_pending)); // a different backend's
                                                               // sessions shouldn't fulfill
                                                               // a stale pending name
    mutex_unlock(&state->lock);

    if (old_backend) {
        old_vtable->destroy(old_backend);
    }

    audio_backend_t *new_backend = backend_vtable->create();
    if (new_backend) {
        backend_vtable->set_callbacks(new_backend, on_session_added, on_session_updated,
                                      on_session_removed, state);
    }

    mutex_lock(&state->lock);
    state->vtable = backend_vtable;
    state->backend = new_backend;
    mutex_unlock(&state->lock);
}

size_t mixer_state_list_sessions(mixer_state_t *state, audio_session_t *out, size_t max_out)
{
    mutex_lock(&state->lock);
    size_t n = state->session_count < max_out ? state->session_count : max_out;
    memcpy(out, state->sessions, n * sizeof(audio_session_t));
    mutex_unlock(&state->lock);

    // Same as mixer_state_build_levels_packet(): a muted session should look
    // silent on the meter, not show the source's raw pre-mute loudness.
    for (size_t i = 0; i < n; i++) {
        if (out[i].muted) {
            out[i].peak = 0.0f;
        }
    }
    return n;
}

bool mixer_state_assign_slot(mixer_state_t *state, int slot, uint32_t session_id)
{
    if (slot < 0 || slot >= MIXER_CHANNELS) {
        return false;
    }

    mutex_lock(&state->lock);

    if (session_id == MIXER_MASTER_SESSION_ID) {
        // Not one of state->sessions[] -- there's nothing to look up here.
        // The real volume/peak/muted arrive from the next mixer_state_poll()
        // tick's get_master() sync; these are just reasonable placeholders
        // until then (matches "assigned" behaving usably the instant a knob
        // is turned, same as a fresh per-app assignment does via the lookup
        // below).
        state->slots[slot].assigned = true;
        state->slots[slot].session_id = MIXER_MASTER_SESSION_ID;
        state->slots[slot].volume = 100;
        state->slots[slot].peak = 0;
        state->slots[slot].muted = false;
        copy_slot_name(state->slots[slot].name, "Master");
        state->has_pending[slot] = false;
        mutex_unlock(&state->lock);
        return true;
    }

    int idx = find_session_index_locked(state, session_id);
    if (idx < 0) {
        mutex_unlock(&state->lock);
        return false;
    }

    state->slots[slot].assigned = true;
    state->slots[slot].session_id = session_id;
    state->slots[slot].volume = (uint8_t)lroundf(state->sessions[idx].volume * 100.0f);
    state->slots[slot].peak = (uint8_t)lroundf(state->sessions[idx].peak * 100.0f);
    state->slots[slot].muted = state->sessions[idx].muted;
    copy_slot_name(state->slots[slot].name, state->sessions[idx].name);
    state->has_pending[slot] = false; // manual assignment cancels any pending reconnect-by-name

    mutex_unlock(&state->lock);
    return true;
}

bool mixer_state_clear_slot(mixer_state_t *state, int slot)
{
    if (slot < 0 || slot >= MIXER_CHANNELS) {
        return false;
    }
    mutex_lock(&state->lock);
    state->slots[slot] = (mixer_slot_t){0};
    state->has_pending[slot] = false;
    mutex_unlock(&state->lock);
    return true;
}

void mixer_state_set_pending_assignment(mixer_state_t *state, int slot, const char *name)
{
    if (slot < 0 || slot >= MIXER_CHANNELS) {
        return;
    }

    mutex_lock(&state->lock);
    if (!assign_slot_by_name_locked(state, slot, name)) {
        memset(state->pending_names[slot], 0, CHANNEL_NAME_LEN);
        strncpy(state->pending_names[slot], name, CHANNEL_NAME_LEN - 1);
        state->has_pending[slot] = true;
    }
    mutex_unlock(&state->lock);
}

bool mixer_state_get_slot(mixer_state_t *state, int slot, mixer_slot_t *out)
{
    if (slot < 0 || slot >= MIXER_CHANNELS) {
        return false;
    }
    mutex_lock(&state->lock);
    *out = state->slots[slot];
    mutex_unlock(&state->lock);
    return true;
}

static bool set_slot_volume_locked(mixer_state_t *state, int slot, uint8_t volume_percent)
{
    mixer_slot_t *s = &state->slots[slot];
    if (!s->assigned) {
        return false;
    }

    uint32_t session_id = s->session_id;
    bool is_master = session_id == MIXER_MASTER_SESSION_ID;
    const audio_backend_vtable_t *vtable = state->vtable;
    audio_backend_t *backend = state->backend;
    mutex_unlock(&state->lock);
    float volume = volume_percent / 100.0f;
    bool ok = is_master ? (backend && vtable->set_master_volume &&
                           vtable->set_master_volume(backend, volume))
                        : (backend && vtable->set_volume(backend, session_id, volume));
    mutex_lock(&state->lock);

    if (ok) {
        // Re-check the slot is still pointing at the same session -- it may
        // have been reassigned/cleared while the lock was released above.
        if (s->assigned && s->session_id == session_id) {
            s->volume = volume_percent;
        }
    }
    return ok;
}

bool mixer_state_adjust_slot_volume(mixer_state_t *state, int slot, int delta_percent)
{
    if (slot < 0 || slot >= MIXER_CHANNELS) {
        return false;
    }

    mutex_lock(&state->lock);
    if (!state->slots[slot].assigned) {
        mutex_unlock(&state->lock);
        return false;
    }

    int new_volume = (int)state->slots[slot].volume + delta_percent;
    if (new_volume < 0) new_volume = 0;
    if (new_volume > 100) new_volume = 100;

    bool ok = set_slot_volume_locked(state, slot, (uint8_t)new_volume);
    mutex_unlock(&state->lock);
    return ok;
}

bool mixer_state_set_slot_volume(mixer_state_t *state, int slot, uint8_t volume_percent)
{
    if (slot < 0 || slot >= MIXER_CHANNELS) {
        return false;
    }
    if (volume_percent > 100) {
        volume_percent = 100;
    }

    mutex_lock(&state->lock);
    bool ok = set_slot_volume_locked(state, slot, volume_percent);
    mutex_unlock(&state->lock);
    return ok;
}

bool mixer_state_toggle_slot_mute(mixer_state_t *state, int slot)
{
    if (slot < 0 || slot >= MIXER_CHANNELS) {
        return false;
    }

    mutex_lock(&state->lock);
    mixer_slot_t *s = &state->slots[slot];
    if (!s->assigned) {
        mutex_unlock(&state->lock);
        return false;
    }

    uint32_t session_id = s->session_id;
    bool is_master = session_id == MIXER_MASTER_SESSION_ID;
    // Master isn't in state->sessions[], so its current muted state (last
    // synced from get_master()) lives only on the slot itself.
    bool new_muted;
    if (is_master) {
        new_muted = !s->muted;
    } else {
        int idx = find_session_index_locked(state, session_id);
        new_muted = idx >= 0 ? !state->sessions[idx].muted : true;
    }
    const audio_backend_vtable_t *vtable = state->vtable;
    audio_backend_t *backend = state->backend;

    mutex_unlock(&state->lock);
    bool ok = is_master ? (backend && vtable->set_master_muted &&
                           vtable->set_master_muted(backend, new_muted))
                        : (backend && vtable->set_muted(backend, session_id, new_muted));
    mutex_lock(&state->lock);

    if (ok) {
        if (is_master) {
            // Re-check the slot is still master-bound -- it may have been
            // reassigned/cleared while the lock was released above (the
            // same re-check the non-master branch already does via
            // find_session_index_locked() below).
            if (s->assigned && s->session_id == MIXER_MASTER_SESSION_ID) {
                s->muted = new_muted;
            }
        } else {
            int idx = find_session_index_locked(state, session_id);
            if (idx >= 0) {
                state->sessions[idx].muted = new_muted;
            }
        }
    }
    mutex_unlock(&state->lock);
    return ok;
}

void mixer_state_build_levels_packet(mixer_state_t *state, levels_packet *out)
{
    channel_level channels[MIXER_CHANNELS];

    mutex_lock(&state->lock);
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        mixer_slot_t *s = &state->slots[i];
        channels[i].volume = s->assigned ? s->volume : 0;
        // Muted reads as a flat 0 on the meter rather than the source's raw
        // (pre-mute) loudness -- a muted channel should visually look
        // silent, even though the backend still measures the source itself.
        channels[i].left = (s->assigned && !s->muted) ? s->peak : 0;
        channels[i].right = (s->assigned && !s->muted) ? s->peak : 0;
        channels[i].muted = s->assigned && s->muted;
    }
    mutex_unlock(&state->lock);

    protocol_build_levels_packet(out, channels);
}

void mixer_state_build_metadata_packet(mixer_state_t *state, metadata_packet *out)
{
    char names[MIXER_CHANNELS][CHANNEL_NAME_LEN];

    mutex_lock(&state->lock);
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        memset(names[i], 0, CHANNEL_NAME_LEN);
        if (state->slots[i].assigned) {
            strncpy(names[i], state->slots[i].name, CHANNEL_NAME_LEN);
        }
    }
    mutex_unlock(&state->lock);

    protocol_build_metadata_packet(out, names);
}
