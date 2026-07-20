// Fake audio_backend for the client's "Simulation" mode: reports
// MIXER_CHANNELS fixed sessions with no real audio behind them, so the
// device's mixer/VU meters can be exercised entirely from the UI. See
// platform/audio_simulated.h.

#include "platform/audio_simulated.h"
#include "platform/mutex.h"
#include "protocol.h" // MIXER_CHANNELS -- one source of truth for the fake channel count
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float volume; // 0.0-1.0, set via the vtable's set_volume (e.g. a knob's slider)
    float level;  // 0.0-1.0, user-set ceiling via audio_simulated_set_level (the simulation panel)
    float peak;   // 0.0-1.0, the animated value actually reported -- eases toward wander_target
    float wander_target; // 0.0-1.0, current easing target; re-picked occasionally within [0, level]
    uint32_t rng;         // per-channel xorshift32 state, seeded distinctly so channels don't move
                          // in lockstep (four identical meters would look obviously fake)
    bool muted;
} sim_channel_t;

// Wire id base for the fake sessions, well clear of real WASAPI/PipeWire id
// ranges. Channel `index`'s id is this plus index.
#define SIM_SESSION_ID_BASE 0xF0000000u

struct audio_backend {
    audio_session_cb on_added;
    audio_session_cb on_updated;
    audio_session_cb on_removed;
    void *user_data;
    bool announced; // whether the initial on_added batch has fired yet
};

// File-static rather than per-instance: audio_simulated_set_level() is
// called from the UI thread with no handle to the active audio_backend_t,
// so the fake channel values need to live somewhere reachable without one.
// Only one simulated backend instance is ever attached to a mixer_state at
// a time, same as every other backend.
static mutex_t g_lock;
static bool g_lock_initialized = false;
static sim_channel_t g_channels[MIXER_CHANNELS];
static struct audio_backend *g_active = NULL;

static void ensure_lock(void)
{
    if (!g_lock_initialized) {
        mutex_init(&g_lock);
        g_lock_initialized = true;
    }
}

// Small, fast, deterministic PRNG -- plenty for cosmetic jitter, not meant
// to be cryptographically random. Must never be seeded to 0 (xorshift is a
// fixed point there).
static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float rand_unit(uint32_t *state) // [0, 1)
{
    return (float)(xorshift32(state) >> 8) / (float)(1u << 24);
}

// Advances one channel's fake peak by one poll tick: eases toward a
// randomly wandering target bounded by the user-set ceiling (`level`), so
// it looks like real audio activity instead of a static held value. Every
// step only closes a fraction of the gap to the target -- an exponential
// smoothing filter -- so motion is always continuous, never a discontinuous
// jump/spike/clip, i.e. no glitches by construction. Muted or zeroed-out
// channels ease down to silence the same smooth way rather than snapping.
static void advance_channel_locked(sim_channel_t *ch)
{
    if (ch->muted || ch->level <= 0.0f) {
        ch->wander_target = 0.0f;
    } else if (rand_unit(&ch->rng) < 0.04f) {
        // Occasionally (~once/sec at the ~60Hz poll rate) pick a new target
        // within the ceiling, simulating a transient in the fake source.
        float floor = ch->level * 0.35f;
        ch->wander_target = floor + rand_unit(&ch->rng) * (ch->level - floor);
    }

    ch->peak += (ch->wander_target - ch->peak) * 0.15f;
    if (ch->peak < 0.0f) {
        ch->peak = 0.0f;
    } else if (ch->peak > 1.0f) {
        ch->peak = 1.0f;
    }
}

// Caller must hold g_lock.
static void session_for_locked(int index, audio_session_t *out)
{
    out->id = SIM_SESSION_ID_BASE + (uint32_t)index;
    snprintf(out->name, AUDIO_SESSION_NAME_LEN, "Simulated %d", index + 1);
    out->volume = g_channels[index].volume;
    out->peak = g_channels[index].peak;
    out->muted = g_channels[index].muted;
}

static audio_backend_t *sim_create(void)
{
    ensure_lock();

    audio_backend_t *backend = calloc(1, sizeof(audio_backend_t));
    if (!backend) {
        return NULL;
    }

    mutex_lock(&g_lock);
    memset(g_channels, 0, sizeof(g_channels));
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        g_channels[i].volume = 1.0f; // 100%, matches a freshly-opened real app's usual default
        // Non-zero out of the box so enabling simulation is immediately
        // audible/visible without first having to find and drag a level
        // slider -- audio_simulated_set_level() still overrides this per
        // channel afterwards.
        g_channels[i].level = 0.6f;
        // Distinct, always-nonzero seed per channel.
        uint32_t seed = (uint32_t)(i + 1) * 2654435761u;
        g_channels[i].rng = seed ? seed : 1u;
    }
    g_active = backend;
    mutex_unlock(&g_lock);

    return backend;
}

static void sim_destroy(audio_backend_t *backend)
{
    if (!backend) {
        return;
    }
    mutex_lock(&g_lock);
    if (g_active == backend) {
        g_active = NULL;
    }
    mutex_unlock(&g_lock);
    free(backend);
}

// Deliberately does not fire on_added from create(): mixer_state_create()/
// mixer_state_set_backend() call set_callbacks() *after* create() returns,
// so an early fire here would go to a NULL callback and be silently
// dropped (same reasoning as audio_windows_wasapi.c's wasapi_create()).
// The first poll() after callbacks are attached announces all channels;
// every poll after that advances and reports all of them, same as a real
// backend continuously re-reading its meters.
static void sim_poll(audio_backend_t *backend)
{
    audio_session_t out[MIXER_CHANNELS];
    bool first_poll;

    mutex_lock(&g_lock);
    first_poll = !backend->announced;
    backend->announced = true;
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        if (!first_poll) {
            advance_channel_locked(&g_channels[i]);
        }
        session_for_locked(i, &out[i]);
    }
    mutex_unlock(&g_lock);

    audio_session_cb cb = first_poll ? backend->on_added : backend->on_updated;
    if (!cb) {
        return;
    }
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        cb(&out[i], backend->user_data);
    }
}

static bool sim_set_volume(audio_backend_t *backend, uint32_t session_id, float volume)
{
    (void)backend;
    if (session_id < SIM_SESSION_ID_BASE || session_id - SIM_SESSION_ID_BASE >= MIXER_CHANNELS) {
        return false;
    }
    int index = (int)(session_id - SIM_SESSION_ID_BASE);

    mutex_lock(&g_lock);
    g_channels[index].volume = volume;
    mutex_unlock(&g_lock);
    return true;
}

static bool sim_set_muted(audio_backend_t *backend, uint32_t session_id, bool muted)
{
    (void)backend;
    if (session_id < SIM_SESSION_ID_BASE || session_id - SIM_SESSION_ID_BASE >= MIXER_CHANNELS) {
        return false;
    }
    int index = (int)(session_id - SIM_SESSION_ID_BASE);

    mutex_lock(&g_lock);
    g_channels[index].muted = muted;
    mutex_unlock(&g_lock);
    return true;
}

static void sim_set_callbacks(audio_backend_t *backend, audio_session_cb on_added,
                              audio_session_cb on_updated, audio_session_cb on_removed,
                              void *user_data)
{
    backend->on_added = on_added;
    backend->on_updated = on_updated;
    backend->on_removed = on_removed;
    backend->user_data = user_data;
}

static const audio_backend_vtable_t g_sim_vtable = {
    .create = sim_create,
    .destroy = sim_destroy,
    .poll = sim_poll,
    .set_volume = sim_set_volume,
    .set_muted = sim_set_muted,
    .set_callbacks = sim_set_callbacks,
};

const audio_backend_vtable_t *audio_simulated_get_vtable(void) { return &g_sim_vtable; }

uint32_t audio_simulated_session_id(int index) { return SIM_SESSION_ID_BASE + (uint32_t)index; }

bool audio_simulated_set_level(int index, uint8_t level_percent)
{
    if (index < 0 || index >= MIXER_CHANNELS) {
        return false;
    }

    ensure_lock();
    mutex_lock(&g_lock);
    bool active = g_active != NULL;
    if (active) {
        if (level_percent > 100) {
            level_percent = 100;
        }
        g_channels[index].level = level_percent / 100.0f;
    }
    mutex_unlock(&g_lock);
    return active;
}
