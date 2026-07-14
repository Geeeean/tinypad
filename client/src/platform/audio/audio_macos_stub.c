// Placeholder audio_backend for macOS: reports zero sessions and rejects
// every mutation. Device connectivity, macro buttons, and the UI shell all
// work fine on top of this -- only the mixer/VU-meter data stays empty
// until a real Core Audio (AudioObjectGetPropertyData / process-tap) backend
// replaces this file.

#include "platform/audio_backend.h"
#include <stdlib.h>

struct audio_backend {
    audio_session_cb on_added;
    audio_session_cb on_updated;
    audio_session_cb on_removed;
    void *user_data;
};

static audio_backend_t *stub_create(void) { return calloc(1, sizeof(audio_backend_t)); }

static void stub_destroy(audio_backend_t *backend) { free(backend); }

static void stub_poll(audio_backend_t *backend) { (void)backend; }

static bool stub_set_volume(audio_backend_t *backend, uint32_t session_id, float volume)
{
    (void)backend;
    (void)session_id;
    (void)volume;
    return false;
}

static bool stub_set_muted(audio_backend_t *backend, uint32_t session_id, bool muted)
{
    (void)backend;
    (void)session_id;
    (void)muted;
    return false;
}

static void stub_set_callbacks(audio_backend_t *backend, audio_session_cb on_added,
                               audio_session_cb on_updated, audio_session_cb on_removed,
                               void *user_data)
{
    backend->on_added = on_added;
    backend->on_updated = on_updated;
    backend->on_removed = on_removed;
    backend->user_data = user_data;
}

static const audio_backend_vtable_t vtable = {
    .create = stub_create,
    .destroy = stub_destroy,
    .poll = stub_poll,
    .set_volume = stub_set_volume,
    .set_muted = stub_set_muted,
    .set_callbacks = stub_set_callbacks,
};

const audio_backend_vtable_t *audio_backend_get_vtable(void) { return &vtable; }
