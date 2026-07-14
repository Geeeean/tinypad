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

#include <windows.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include "platform/audio_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// mmdeviceapi.h/audiopolicy.h/endpointvolume.h only *declare* these as
// `extern const IID ...` -- on current Windows SDKs, #define INITGUID
// before including them (the traditional fix) does not reliably make them
// *defined* for C translation units, only C++ ones (which instead
// synthesize the value from __uuidof(), sidestepping this entirely). So
// the actual values are defined here by hand instead of relying on the
// headers. <initguid.h> forces DEFINE_GUID back into "instantiate" mode
// regardless of any earlier header's declare-only expansion; values are
// from the Windows SDK (Core Audio APIs, stable since Vista) and
// cross-checked against mingw-w64's headers.
#include <initguid.h>
DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92,
            0x91, 0x69, 0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6,
            0x36, 0x17, 0xe6);
DEFINE_GUID(IID_IAudioSessionManager2, 0x77aa99a0, 0x1bd6, 0x484f, 0x8b, 0xc7, 0x2c, 0x65, 0x4c,
            0x9a, 0x9b, 0x6f);
DEFINE_GUID(IID_IAudioSessionControl2, 0xbfb7ff88, 0x7239, 0x4fc9, 0x8f, 0xa2, 0x07, 0xc9, 0x50,
            0xbe, 0x9c, 0x6d);
DEFINE_GUID(IID_ISimpleAudioVolume, 0x87ce5498, 0x68d6, 0x44e5, 0x92, 0x15, 0x6d, 0xa4, 0x7e, 0xf8,
            0x83, 0xd8);
DEFINE_GUID(IID_IAudioMeterInformation, 0xc02216f6, 0x8c67, 0x4b5b, 0x9d, 0x00, 0xd0, 0x08, 0xe7,
            0x3e, 0x00, 0x64);

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
    HRESULT hr_enum = backend->session_manager->lpVtbl->GetSessionEnumerator(
        backend->session_manager, &session_enum);
    if (FAILED(hr_enum)) {
        fprintf(stderr, "wasapi: GetSessionEnumerator failed hr=0x%08lx\n", (unsigned long)hr_enum);
        return;
    }

    int count = 0;
    HRESULT hr_count = session_enum->lpVtbl->GetCount(session_enum, &count);
    fprintf(stderr, "wasapi: GetCount hr=0x%08lx count=%d\n", (unsigned long)hr_count, count);

    bool seen[MAX_BACKEND_SESSIONS] = {0};

    for (int i = 0; i < count; i++) {
        IAudioSessionControl *control = NULL;
        HRESULT hr_get = session_enum->lpVtbl->GetSession(session_enum, i, &control);
        if (FAILED(hr_get) || !control) {
            fprintf(stderr, "wasapi: [%d] GetSession failed hr=0x%08lx\n", i, (unsigned long)hr_get);
            continue;
        }

        AudioSessionState state;
        control->lpVtbl->GetState(control, &state);
        fprintf(stderr, "wasapi: [%d] state=%d\n", i, (int)state);

        IAudioSessionControl2 *control2 = NULL;
        control->lpVtbl->QueryInterface(control, &IID_IAudioSessionControl2, (void **)&control2);
        control->lpVtbl->Release(control);
        if (!control2) {
            continue;
        }

        DWORD pid = 0;
        control2->lpVtbl->GetProcessId(control2, &pid);
        fprintf(stderr, "wasapi: [%d] pid=%lu\n", i, (unsigned long)pid);

        if (state == AudioSessionStateExpired || pid == 0) {
            fprintf(stderr, "wasapi: [%d] skipped (expired or pid 0)\n", i);
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

        fprintf(stderr, "wasapi: [%d] added name='%s' on_added=%p\n", i, slot->name,
                (void *)backend->on_added);
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

    // Deliberately no initial rescan_sessions() call here: the caller
    // (mixer_state_create()) wires up on_added/on_updated/on_removed via
    // set_callbacks() *after* create() returns, so scanning now would find
    // every already-playing session (e.g. a browser tab opened before this
    // app started) but silently drop it -- on_added would be NULL, and
    // every later rescan would then treat it as already-known and never
    // fire the callback for it at all. Let the first real scan happen
    // through the normal wasapi_poll() path once callbacks are attached.
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
