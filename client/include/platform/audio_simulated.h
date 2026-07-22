#pragma once

// A fourth audio_backend_vtable_t implementation (see audio_backend.h) that
// never touches real OS audio: it reports a fixed set of MIXER_CHANNELS fake
// sessions ("Simulated 1".."Simulated N") whose volume/peak are driven by
// audio_simulated_set_level() and the vtable's own set_volume(), instead of
// a real audio stack. Also implements get_master()/set_master_volume()/
// set_master_muted() with the same fake wandering-peak model, so a knob can
// be assigned to "Master" in Simulation mode too. Lets the UI's
// "Simulation" toggle (ui/ui_bridge.c) exercise the device's VU
// meters/mixer without any audio actually playing.

#include "platform/audio_backend.h"
#include <stdint.h>

const audio_backend_vtable_t *audio_simulated_get_vtable(void);

// Sets channel `index`'s (0..MIXER_CHANNELS-1 -- see protocol.h) simulated
// peak level, wire scale 0-100. Returns false (no-op) if index is out of
// range or no simulated backend instance is currently active (simulation
// not enabled). Picked up by the next poll() on the vtable returned above.
bool audio_simulated_set_level(int index, uint8_t level_percent);

// The audio_session_t.id a simulated channel reports/expects, e.g. for
// auto-assigning it to a mixer slot right after enabling simulation without
// the caller needing to know the internal id scheme.
uint32_t audio_simulated_session_id(int index);
