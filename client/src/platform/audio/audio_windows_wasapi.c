// WASAPI audio_backend implementation: per-application session volume/mute
// control and peak metering via the default render endpoint's
// IAudioSessionManager2. Not build-verified on this (non-Windows)
// development machine -- review carefully and test on real hardware.
//
// Session add/remove is detected by periodically re-enumerating
// IAudioSessionEnumerator (every WASAPI_ENUM_INTERVAL_MS); volume/mute/peak
// for already-known sessions are refreshed on every poll() call, which is
// cheap (no enumeration, just a handful of interface calls per session).
// This mirrors the PipeWire backend's split between "rare structural
// change" and "frequent level update" without needing
// IMMNotificationClient/IAudioSessionNotification plumbing.

#define INITGUID
#include "platform/audio_backend.h"
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_BACKEND_SESSIONS 64
#define WASAPI_ENUM_INTERVAL_MS 200

typedef struct {
    bool active;
    uint32_t pid;
    char name[AUDIO_SESSION_NAME_LEN];

    IAudioSessionControl2 *control;
    ISimpleAudioVolume *volume_ctrl;
    IAudioMeterInformation *meter;

    float last_volume;
    float last_peak;
    bool last_muted;
} backend_session_t;

struct audio_backend {
    IMMDeviceEnumerator *enumerator;
    IMMDevice *device;
    IAudioSessionManager2 *session_manager;

    backend_session_t sessions[MAX_BACKEND_SESSIONS];
    ULONGLONG last_enum_tick;

    audio_session_cb on_added;
    audio_session_cb on_updated;
    audio_session_cb on_removed;
    void *user_data;
};

static void build_session_snapshot(backend_session_t *s, audio_session_t *out)
{
    out->id = s->pid;
    snprintf(out->name, sizeof(out->name), "%s", s->name);
    out->volume = s->last_volume;
    out->peak = s->last_peak;
    out->muted = s->last_muted;
}

static void resolve_process_name(uint32_t pid, char *out, size_t out_size)
{
    snprintf(out, out_size, "pid %u", pid);

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!process) {
        return;
    }

    WCHAR path[MAX_PATH];
    DWORD len = MAX_PATH;
    if (QueryFullProcessImageNameW(process, 0, path, &len)) {
        WCHAR *base = wcsrchr(path, L'\\');
        base = base ? base + 1 : path;
        WideCharToMultiByte(CP_UTF8, 0, base, -1, out, (int)out_size, NULL, NULL);
    }

    CloseHandle(process);
}

static backend_session_t *find_session_by_pid(audio_backend_t *backend, uint32_t pid)
{
    for (int i = 0; i < MAX_BACKEND_SESSIONS; i++) {
        if (backend->sessions[i].active && backend->sessions[i].pid == pid) {
            return &backend->sessions[i];
        }
    }
    return NULL;
}

static void release_session(backend_session_t *s)
{
    if (s->meter) s->meter->lpVtbl->Release(s->meter);
    if (s->volume_ctrl) s->volume_ctrl->lpVtbl->Release(s->volume_ctrl);
    if (s->control) s->control->lpVtbl->Release(s->control);
    memset(s, 0, sizeof(*s));
}

static void refresh_levels(audio_backend_t *backend, backend_session_t *s)
{
    float volume = s->last_volume;
    BOOL muted = s->last_muted;
    float peak = 0.0f;

    if (s->volume_ctrl) {
        s->volume_ctrl->lpVtbl->GetMasterVolume(s->volume_ctrl, &volume);
        s->volume_ctrl->lpVtbl->GetMute(s->volume_ctrl, &muted);
    }
    if (s->meter) {
        s->meter->lpVtbl->GetPeakValue(s->meter, &peak);
    }

    bool changed = (volume != s->last_volume) || ((bool)muted != s->last_muted) ||
                  (peak != s->last_peak);

    s->last_volume = volume;
    s->last_muted = (bool)muted;
    s->last_peak = peak;

    if (changed && backend->on_updated) {
        audio_session_t snapshot;
        build_session_snapshot(s, &snapshot);
        backend->on_updated(&snapshot, backend->user_data);
    }
}

// Re-scans live sessions: adds newly-seen ones, drops ones that expired.
static void rescan_sessions(audio_backend_t *backend)
{
    if (!backend->session_manager) {
        return;
    }

    IAudioSessionEnumerator *session_enum = NULL;
    if (FAILED(backend->session_manager->lpVtbl->GetSessionEnumerator(backend->session_manager,
                                                                       &session_enum))) {
        return;
    }

    int count = 0;
    session_enum->lpVtbl->GetCount(session_enum, &count);

    bool seen[MAX_BACKEND_SESSIONS] = {0};

    for (int i = 0; i < count; i++) {
        IAudioSessionControl *control = NULL;
        if (FAILED(session_enum->lpVtbl->GetSession(session_enum, i, &control)) || !control) {
            continue;
        }

        AudioSessionState state;
        control->lpVtbl->GetState(control, &state);

        IAudioSessionControl2 *control2 = NULL;
        control->lpVtbl->QueryInterface(control, &IID_IAudioSessionControl2, (void **)&control2);
        control->lpVtbl->Release(control);
        if (!control2) {
            continue;
        }

        DWORD pid = 0;
        control2->lpVtbl->GetProcessId(control2, &pid);

        if (state == AudioSessionStateExpired || pid == 0) {
            control2->lpVtbl->Release(control2);
            continue;
        }

        backend_session_t *existing = find_session_by_pid(backend, pid);
        if (existing) {
            for (int j = 0; j < MAX_BACKEND_SESSIONS; j++) {
                if (&backend->sessions[j] == existing) {
                    seen[j] = true;
                    break;
                }
            }
            control2->lpVtbl->Release(control2);
            continue;
        }

        backend_session_t *slot = NULL;
        int slot_index = -1;
        for (int j = 0; j < MAX_BACKEND_SESSIONS; j++) {
            if (!backend->sessions[j].active) {
                slot = &backend->sessions[j];
                slot_index = j;
                break;
            }
        }
        if (!slot) {
            control2->lpVtbl->Release(control2);
            continue;
        }

        slot->active = true;
        slot->pid = pid;
        slot->control = control2;
        control2->lpVtbl->QueryInterface(control2, &IID_ISimpleAudioVolume,
                                         (void **)&slot->volume_ctrl);
        control2->lpVtbl->QueryInterface(control2, &IID_IAudioMeterInformation,
                                         (void **)&slot->meter);

        resolve_process_name(pid, slot->name, sizeof(slot->name));
        if (slot->volume_ctrl) {
            slot->volume_ctrl->lpVtbl->GetMasterVolume(slot->volume_ctrl, &slot->last_volume);
            BOOL muted = FALSE;
            slot->volume_ctrl->lpVtbl->GetMute(slot->volume_ctrl, &muted);
            slot->last_muted = (bool)muted;
        }

        seen[slot_index] = true;

        if (backend->on_added) {
            audio_session_t snapshot;
            build_session_snapshot(slot, &snapshot);
            backend->on_added(&snapshot, backend->user_data);
        }
    }

    session_enum->lpVtbl->Release(session_enum);

    for (int j = 0; j < MAX_BACKEND_SESSIONS; j++) {
        if (backend->sessions[j].active && !seen[j]) {
            audio_session_t snapshot;
            build_session_snapshot(&backend->sessions[j], &snapshot);
            release_session(&backend->sessions[j]);
            if (backend->on_removed) {
                backend->on_removed(&snapshot, backend->user_data);
            }
        }
    }
}

static audio_backend_t *wasapi_create(void)
{
    audio_backend_t *backend = calloc(1, sizeof(audio_backend_t));
    if (!backend) {
        return NULL;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    // RPC_E_CHANGED_MODE means COM was already initialized with a different
    // concurrency model by the UI layer (e.g. the webview) on this thread;
    // that's fine, we just reuse it.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        free(backend);
        return NULL;
    }

    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                &IID_IMMDeviceEnumerator, (void **)&backend->enumerator))) {
        free(backend);
        return NULL;
    }

    if (FAILED(backend->enumerator->lpVtbl->GetDefaultAudioEndpoint(
            backend->enumerator, eRender, eConsole, &backend->device))) {
        backend->enumerator->lpVtbl->Release(backend->enumerator);
        free(backend);
        return NULL;
    }

    if (FAILED(backend->device->lpVtbl->Activate(backend->device, &IID_IAudioSessionManager2, CLSCTX_ALL,
                                                  NULL, (void **)&backend->session_manager))) {
        backend->device->lpVtbl->Release(backend->device);
        backend->enumerator->lpVtbl->Release(backend->enumerator);
        free(backend);
        return NULL;
    }

    rescan_sessions(backend);
    backend->last_enum_tick = GetTickCount64();

    return backend;
}

static void wasapi_destroy(audio_backend_t *backend)
{
    if (!backend) {
        return;
    }

    for (int i = 0; i < MAX_BACKEND_SESSIONS; i++) {
        if (backend->sessions[i].active) {
            release_session(&backend->sessions[i]);
        }
    }

    if (backend->session_manager) backend->session_manager->lpVtbl->Release(backend->session_manager);
    if (backend->device) backend->device->lpVtbl->Release(backend->device);
    if (backend->enumerator) backend->enumerator->lpVtbl->Release(backend->enumerator);

    CoUninitialize();
    free(backend);
}

static void wasapi_poll(audio_backend_t *backend)
{
    ULONGLONG now = GetTickCount64();
    if (now - backend->last_enum_tick >= WASAPI_ENUM_INTERVAL_MS) {
        rescan_sessions(backend);
        backend->last_enum_tick = now;
    }

    for (int i = 0; i < MAX_BACKEND_SESSIONS; i++) {
        if (backend->sessions[i].active) {
            refresh_levels(backend, &backend->sessions[i]);
        }
    }
}

static bool wasapi_set_volume(audio_backend_t *backend, uint32_t session_id, float volume)
{
    backend_session_t *s = find_session_by_pid(backend, session_id);
    if (!s || !s->volume_ctrl) {
        return false;
    }
    return SUCCEEDED(s->volume_ctrl->lpVtbl->SetMasterVolume(s->volume_ctrl, volume, NULL));
}

static bool wasapi_set_muted(audio_backend_t *backend, uint32_t session_id, bool muted)
{
    backend_session_t *s = find_session_by_pid(backend, session_id);
    if (!s || !s->volume_ctrl) {
        return false;
    }
    return SUCCEEDED(s->volume_ctrl->lpVtbl->SetMute(s->volume_ctrl, muted, NULL));
}

static void wasapi_set_callbacks(audio_backend_t *backend, audio_session_cb on_added,
                                 audio_session_cb on_updated, audio_session_cb on_removed,
                                 void *user_data)
{
    backend->on_added = on_added;
    backend->on_updated = on_updated;
    backend->on_removed = on_removed;
    backend->user_data = user_data;
}

static const audio_backend_vtable_t vtable = {
    .create = wasapi_create,
    .destroy = wasapi_destroy,
    .poll = wasapi_poll,
    .set_volume = wasapi_set_volume,
    .set_muted = wasapi_set_muted,
    .set_callbacks = wasapi_set_callbacks,
};

const audio_backend_vtable_t *audio_backend_get_vtable(void) { return &vtable; }
