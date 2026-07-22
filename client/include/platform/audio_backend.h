#pragma once

// Platform audio abstraction: per-application session enumeration, volume
// control, and peak metering. Exactly one implementation is compiled in:
//   - src/platform/audio/audio_linux_pipewire.c    (PipeWire)
//   - src/platform/audio/audio_windows_wasapi.c    (WASAPI session APIs)
//   - src/platform/audio/audio_macos_coreaudio.m    (Core Audio process taps)
// All three expose audio_backend_get_vtable(), selected by CMakeLists.txt,
// so core/mixer_state.c never needs #ifdef.

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_SESSION_NAME_LEN 128

typedef struct {
    uint32_t id; // backend-specific (PipeWire node id / Windows process id), opaque to core
    char name[AUDIO_SESSION_NAME_LEN];
    float volume; // 0.0 - 1.0
    float peak;   // 0.0 - 1.0, most recent peak magnitude (mono -- see mixer_state)
    bool muted;
} audio_session_t;

typedef void (*audio_session_cb)(const audio_session_t *session, void *user_data);

typedef struct audio_backend audio_backend_t; // opaque, defined per platform impl

typedef struct {
    audio_backend_t *(*create)(void);
    void (*destroy)(audio_backend_t *backend);

    // Pumps backend-specific events (new/removed sessions, volume/peak
    // changes) and fires the callbacks below. Driven from the main loop at
    // ~30-60Hz; backends with nothing to poll return immediately.
    void (*poll)(audio_backend_t *backend);

    bool (*set_volume)(audio_backend_t *backend, uint32_t session_id, float volume);
    bool (*set_muted)(audio_backend_t *backend, uint32_t session_id, bool muted);

    // Callbacks may fire from a backend-owned thread (e.g. PipeWire's thread
    // loop) rather than from poll()'s caller -- consumers must treat them as
    // async and not assume they run on the polling thread.
    void (*set_callbacks)(audio_backend_t *backend, audio_session_cb on_added,
                          audio_session_cb on_updated, audio_session_cb on_removed,
                          void *user_data);

    // Optional: the system's total/master output, as opposed to any one
    // application's session -- a device-level volume/mute (WASAPI's
    // IAudioEndpointVolume, macOS's kAudioDevicePropertyVolumeScalar on the
    // default output device, PipeWire's default sink node), not a process
    // tap/session. NULL if a backend doesn't support it; core/mixer_state.c
    // checks before calling. get_master() is polled once per
    // mixer_state_poll() tick, same cadence as poll() itself; its `id`
    // field is ignored by the caller (core/mixer_state.c substitutes its
    // own reserved MIXER_MASTER_SESSION_ID).
    bool (*get_master)(audio_backend_t *backend, audio_session_t *out);
    bool (*set_master_volume)(audio_backend_t *backend, float volume);
    bool (*set_master_muted)(audio_backend_t *backend, bool muted);
} audio_backend_vtable_t;

const audio_backend_vtable_t *audio_backend_get_vtable(void);
