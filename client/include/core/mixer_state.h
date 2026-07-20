#pragma once

// Device-agnostic mixer model: the full list of audio sessions reported by
// the active audio_backend, plus the MIXER_CHANNELS "slots" that are
// actually wired to the physical device's VU meters/volume knobs. A slot
// holds a reference to one session; device_link snapshots the slots into
// levels_packet/metadata_packet on every tick.

#include "platform/audio_backend.h"
#include "protocol.h"
#include <stddef.h>
#include <stdint.h>

#define MIXER_MAX_SESSIONS 64

typedef struct {
    bool assigned;
    uint32_t session_id;
    char name[CHANNEL_NAME_LEN]; // truncated copy of the session name
    uint8_t volume;              // 0-100, wire scale
    uint8_t peak;                // 0-100, wire scale
    bool muted;
} mixer_slot_t;

typedef struct mixer_state mixer_state_t;

// Takes the vtable of whichever audio_backend was selected at compile time
// (see platform/audio_backend.h); does not take ownership of the pointer
// itself, just the backend instance it creates from it.
mixer_state_t *mixer_state_create(const audio_backend_vtable_t *backend_vtable);
void mixer_state_destroy(mixer_state_t *state);

// Detaches the current backend (destroying it) and attaches a new one
// created from backend_vtable, e.g. to swap real OS audio for
// audio_simulated's fake sessions and back. Every known session and slot
// assignment is cleared as part of the swap, since they belonged to the old
// backend. Safe to call from a different thread than mixer_state_poll()
// runs on.
void mixer_state_set_backend(mixer_state_t *state, const audio_backend_vtable_t *backend_vtable);

// Pumps the underlying audio backend; call from the main loop every tick.
void mixer_state_poll(mixer_state_t *state);

// Copies up to max_out known sessions into out (for the UI's session
// picker). Returns the number copied.
size_t mixer_state_list_sessions(mixer_state_t *state, audio_session_t *out, size_t max_out);

bool mixer_state_assign_slot(mixer_state_t *state, int slot, uint32_t session_id);
bool mixer_state_clear_slot(mixer_state_t *state, int slot);
bool mixer_state_get_slot(mixer_state_t *state, int slot, mixer_slot_t *out);

// Encoder turn: nudges the assigned session's volume by `delta_percent`
// (positive/negative, wire 0-100 scale, clamped). No-op if the slot has no
// session assigned.
bool mixer_state_adjust_slot_volume(mixer_state_t *state, int slot, int delta_percent);
bool mixer_state_set_slot_volume(mixer_state_t *state, int slot, uint8_t volume_percent);
bool mixer_state_toggle_slot_mute(mixer_state_t *state, int slot);

// Snapshots current slot state into wire packets.
void mixer_state_build_levels_packet(mixer_state_t *state, levels_packet *out);
void mixer_state_build_metadata_packet(mixer_state_t *state, metadata_packet *out);
