#include "ui/ui_bridge.h"
#include "platform/audio_backend.h"
#include "platform/audio_simulated.h"
#include "protocol.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webview/webview.h>

#if defined(_WIN32)
#include <windows.h>
#define PATH_SEP_CHAR '\\'
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#define PATH_SEP_CHAR '/'
#else
#include <limits.h>
#include <unistd.h>
#define PATH_SEP_CHAR '/'
#endif

struct ui_bridge {
    webview_t w;
    mixer_state_t *mixer;
    macro_map_t *macros;
    device_settings_t *settings;
    profile_store_t *profiles;
    bool simulation_enabled;
    // Whether macros/settings/slot assignments have changed since the
    // active profile was last saved/loaded -- explicit save, not auto-save,
    // so the UI needs this to show an "unsaved changes" indicator and
    // confirm before discarding on profile switch.
    bool profile_dirty;
};

// --- tiny string-builder + JSON helpers ---------------------------------
// No JSON library vendored -- the data model is flat enough that hand-rolled
// building/escaping is simpler. Session/app names come from the OS audio
// stack (untrusted) and are eval'd as JS, so escaping must be correct.

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} strbuf_t;

static void sb_init(strbuf_t *sb, char *buf, size_t cap)
{
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    if (cap) {
        buf[0] = '\0';
    }
}

static void sb_appendf(strbuf_t *sb, const char *fmt, ...)
{
    if (sb->len >= sb->cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t written = (size_t)n;
        size_t remaining = sb->cap - sb->len;
        sb->len += written < remaining ? written : remaining;
    }
}

static void sb_append_json_string(strbuf_t *sb, const char *s)
{
    sb_appendf(sb, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': sb_appendf(sb, "\\\""); break;
        case '\\': sb_appendf(sb, "\\\\"); break;
        case '\n': sb_appendf(sb, "\\n"); break;
        case '\r': sb_appendf(sb, "\\r"); break;
        case '\t': sb_appendf(sb, "\\t"); break;
        default:
            if (*p < 0x20) {
                sb_appendf(sb, "\\u%04x", *p);
            } else {
                sb_appendf(sb, "%c", *p);
            }
        }
    }
    sb_appendf(sb, "\"");
}

static void build_state_json(ui_bridge_t *bridge, strbuf_t *sb)
{
    audio_session_t sessions[MIXER_MAX_SESSIONS];
    size_t n = mixer_state_list_sessions(bridge->mixer, sessions, MIXER_MAX_SESSIONS);

    sb_appendf(sb, "{\"sessions\":[");
    for (size_t i = 0; i < n; i++) {
        if (i) sb_appendf(sb, ",");
        sb_appendf(sb, "{\"id\":%u,\"name\":", sessions[i].id);
        sb_append_json_string(sb, sessions[i].name);
        sb_appendf(sb, ",\"volume\":%.3f,\"peak\":%.3f,\"muted\":%s}", sessions[i].volume,
                  sessions[i].peak, sessions[i].muted ? "true" : "false");
    }

    sb_appendf(sb, "],\"slots\":[");
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        if (i) sb_appendf(sb, ",");
        mixer_slot_t slot;
        mixer_state_get_slot(bridge->mixer, i, &slot);
        sb_appendf(sb, "{\"assigned\":%s,\"sessionId\":%u,\"name\":",
                  slot.assigned ? "true" : "false", slot.session_id);
        sb_append_json_string(sb, slot.assigned ? slot.name : "");
        sb_appendf(sb, ",\"volume\":%u,\"peak\":%u,\"muted\":%s}", slot.volume, slot.peak,
                  slot.muted ? "true" : "false");
    }

    sb_appendf(sb, "],\"macros\":[");
    for (int i = 0; i < MACRO_TRIGGER_COUNT; i++) {
        if (i) sb_appendf(sb, ",");
        macro_action_t action = macro_map_get(bridge->macros, (macro_trigger_t)i);
        sb_appendf(sb, "{\"type\":%d,\"targetSlot\":%d,\"steps\":[", (int)action.type,
                  action.target_slot);
        for (int s = 0; s < action.step_count; s++) {
            if (s) sb_appendf(sb, ",");
            sb_appendf(sb, "{\"modifiers\":%u,\"key\":%d}", action.steps[s].modifiers,
                      (int)action.steps[s].key);
        }
        sb_appendf(sb, "]}");
    }

    sb_appendf(sb, "],\"deviceSettings\":{\"guiLayout\":[");
    {
        uint8_t layout[GUI_COMPONENT_COUNT];
        device_settings_get_gui_layout(bridge->settings, layout);
        for (int i = 0; i < GUI_COMPONENT_COUNT; i++) {
            if (i) sb_appendf(sb, ",");
            sb_appendf(sb, "%u", layout[i]);
        }
    }
    sb_appendf(sb, "],\"macroLabels\":[");
    for (int i = 0; i < MACRO_BUTTON_COUNT; i++) {
        if (i) sb_appendf(sb, ",");
        char label[MACRO_LABEL_LEN];
        device_settings_get_macro_label(bridge->settings, i, label, sizeof(label));
        sb_append_json_string(sb, label);
    }
    sb_appendf(sb, "]},\"simulationEnabled\":%s", bridge->simulation_enabled ? "true" : "false");

    sb_appendf(sb, ",\"profiles\":[");
    {
        profile_summary_t profiles[64];
        size_t n_profiles = bridge->profiles ? profile_store_list(bridge->profiles, profiles, 64) : 0;
        for (size_t i = 0; i < n_profiles; i++) {
            if (i) sb_appendf(sb, ",");
            sb_appendf(sb, "{\"id\":%lld,\"name\":", (long long)profiles[i].id);
            sb_append_json_string(sb, profiles[i].name);
            sb_appendf(sb, "}");
        }
    }
    int64_t active_profile_id = -1;
    if (bridge->profiles) {
        profile_store_get_active_id(bridge->profiles, &active_profile_id);
    }
    sb_appendf(sb, "],\"activeProfileId\":%lld,\"profileDirty\":%s}", (long long)active_profile_id,
              bridge->profile_dirty ? "true" : "false");
}

// push_state runs on the background polling thread, but webview_eval() must
// run on the UI thread (WebView2's ExecuteScript is STA-affinitized).
// webview_dispatch() marshals the call over.
static void dispatched_eval(webview_t w, void *arg)
{
    char *js = arg;
    webview_eval(w, js);
    free(js);
}

void ui_bridge_push_state(ui_bridge_t *bridge)
{
    if (!bridge || !bridge->w) {
        return;
    }

    char json_buf[16384];
    strbuf_t sb;
    sb_init(&sb, json_buf, sizeof(json_buf));
    build_state_json(bridge, &sb);

    char js_buf[16384 + 64];
    snprintf(js_buf, sizeof(js_buf), "window.onState && window.onState(%s)", json_buf);

    size_t js_len = strlen(js_buf) + 1;
    char *js_copy = malloc(js_len);
    if (js_copy) {
        memcpy(js_copy, js_buf, js_len);
        webview_dispatch(bridge->w, dispatched_eval, js_copy);
    }
}

// --- JS -> native bound functions --------------------------------------
// Every bound function here only ever takes integer arguments, so a full
// JSON parser is overkill: just split the flat "[1,2,3]" array on top-level
// commas and strtol() each token.
static int parse_int_args(const char *req, long *out, int max_count)
{
    int count = 0;
    const char *p = req;
    while (*p && *p != '[') p++;
    if (*p == '[') p++;

    while (*p && *p != ']' && count < max_count) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || *p == '\0') break;
        char *end;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[count++] = v;
        p = end;
    }
    return count;
}

// Parses a `[<int>,"<string>"]` request (native_set_macro_label's shape).
// Only handles \" and \\ escapes -- not a general JSON string decoder.
static bool parse_int_and_string_arg(const char *req, long *out_int, char *out_str,
                                     size_t out_str_size)
{
    const char *p = req;
    while (*p && *p != '[') p++;
    if (*p != '[') return false;
    p++;

    char *end;
    *out_int = strtol(p, &end, 10);
    if (end == p) return false;
    p = end;

    while (*p == ' ' || *p == ',') p++;
    if (*p != '"') return false;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_str_size) {
        if (*p == '\\' && *(p + 1)) {
            p++;
        }
        out_str[i++] = *p++;
    }
    out_str[i] = '\0';
    return true;
}

static void native_assign_slot(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[2];
    bool ok = parse_int_args(req, args, 2) == 2 &&
             mixer_state_assign_slot(bridge->mixer, (int)args[0], (uint32_t)args[1]);
    if (ok) {
        bridge->profile_dirty = true;
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

static void native_clear_slot(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[1];
    bool ok = parse_int_args(req, args, 1) == 1 && mixer_state_clear_slot(bridge->mixer, (int)args[0]);
    if (ok) {
        bridge->profile_dirty = true;
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

static void native_set_slot_volume(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[2];
    bool ok = parse_int_args(req, args, 2) == 2 &&
             mixer_state_set_slot_volume(bridge->mixer, (int)args[0], (uint8_t)args[1]);
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

static void native_toggle_slot_mute(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[1];
    bool ok =
        parse_int_args(req, args, 1) == 1 && mixer_state_toggle_slot_mute(bridge->mixer, (int)args[0]);
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// arg: 0/1. Swaps mixer_state's backend between the real platform one and
// audio_simulated's fake sessions -- "detaching" from OS audio.
static void native_set_simulation_enabled(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[1];
    bool ok = parse_int_args(req, args, 1) == 1;
    if (ok) {
        bool enabled = args[0] != 0;
        mixer_state_set_backend(bridge->mixer,
                                enabled ? audio_simulated_get_vtable() : audio_backend_get_vtable());
        bridge->simulation_enabled = enabled;

        if (enabled) {
            // audio_simulated announces its sessions on the first poll()
            // after set_callbacks(), which mixer_state_set_backend() just
            // wired up -- force that poll synchronously here instead of
            // waiting for the background thread's next ~16ms tick, so the
            // simulated sessions exist immediately, then wire each one
            // straight to its matching knob. Otherwise simulation would
            // start with every slot unassigned and the device would show no
            // VU activity until the user separately assigned each session,
            // same as they would for a real app -- defeats the point of a
            // one-click "just show me something" toggle.
            mixer_state_poll(bridge->mixer);
            for (int i = 0; i < MIXER_CHANNELS; i++) {
                mixer_state_assign_slot(bridge->mixer, i, audio_simulated_session_id(i));
            }
        }
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// args: [channelIndex, levelPercent]. Only takes effect while simulation is
// enabled (audio_simulated_set_level() no-ops otherwise).
static void native_set_simulated_level(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[2];
    bool ok = parse_int_args(req, args, 2) == 2 &&
             audio_simulated_set_level((int)args[0], (uint8_t)args[1]);
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// args: [trigger, type, targetSlot, modifiers_1, key_1, modifiers_2, key_2, ...].
// targetSlot only matters for MACRO_ACTION_TOGGLE_MUTE_SLOT; the (modifiers,
// key) pairs are a flattened MACRO_ACTION_SEND_KEYSTROKE sequence.
static void native_set_macro(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[3 + 2 * MACRO_KEYSTROKE_MAX_STEPS];
    int n = parse_int_args(req, args, (int)(sizeof(args) / sizeof(args[0])));

    bool ok = false;
    if (n >= 3 && args[0] >= 0 && args[0] < MACRO_TRIGGER_COUNT) {
        macro_action_t action = {
            .type = (macro_action_type_t)args[1],
            .target_slot = (int)args[2],
        };

        int step_pairs = (n - 3) / 2;
        if (step_pairs > MACRO_KEYSTROKE_MAX_STEPS) {
            step_pairs = MACRO_KEYSTROKE_MAX_STEPS;
        }
        for (int s = 0; s < step_pairs; s++) {
            action.steps[s].modifiers = (uint32_t)args[3 + s * 2];
            action.steps[s].key = (macro_key_t)args[3 + s * 2 + 1];
        }
        action.step_count = step_pairs;

        macro_map_set(bridge->macros, (macro_trigger_t)args[0], action);
        fprintf(stderr, "ui: macro assigned: trigger=%ld type=%ld targetSlot=%ld steps=%d\n",
                args[0], args[1], args[2], step_pairs);
        bridge->profile_dirty = true;
        ok = true;
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

static void native_set_macro_label(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long index;
    char label[MACRO_LABEL_LEN];
    bool ok = parse_int_and_string_arg(req, &index, label, sizeof(label)) && index >= 0 &&
             index < MACRO_BUTTON_COUNT;
    if (ok) {
        device_settings_set_macro_label(bridge->settings, (int)index, label);
        fprintf(stderr, "ui: macro label set: index=%ld label=\"%s\"\n", index, label);
        bridge->profile_dirty = true;
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// args: GUI_COMPONENT_COUNT ids, draw order top-to-bottom; GUI_COMPONENT_NONE
// disables that slot. device_settings_set_gui_layout normalizes, so an
// out-of-range or duplicate id here just gets dropped rather than rejecting
// the whole call.
static void native_set_gui_layout(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[GUI_COMPONENT_COUNT];
    bool ok = parse_int_args(req, args, GUI_COMPONENT_COUNT) == GUI_COMPONENT_COUNT;
    if (ok) {
        uint8_t layout[GUI_COMPONENT_COUNT];
        for (int i = 0; i < GUI_COMPONENT_COUNT; i++) {
            layout[i] = (uint8_t)args[i];
        }
        device_settings_set_gui_layout(bridge->settings, layout);
        bridge->profile_dirty = true;
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// --- profile management --------------------------------------------------

// Parses a `["<string>"]` request (a single string argument, no leading
// int) -- native_save_profile_as's shape. Only handles \" and \\ escapes,
// same as parse_int_and_string_arg above.
static bool parse_string_arg(const char *req, char *out_str, size_t out_str_size)
{
    const char *p = req;
    while (*p && *p != '[') p++;
    if (*p != '[') return false;
    p++;

    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_str_size) {
        if (*p == '\\' && *(p + 1)) {
            p++;
        }
        out_str[i++] = *p++;
    }
    out_str[i] = '\0';
    return true;
}

// Overwrites the currently active profile with the current live state.
static void native_save_profile(const char *id, const char *req, void *arg)
{
    (void)req;
    ui_bridge_t *bridge = arg;
    int64_t active_id;
    bool ok = bridge->profiles && profile_store_get_active_id(bridge->profiles, &active_id) &&
             profile_store_save(bridge->profiles, active_id, bridge->macros, bridge->settings,
                                bridge->mixer);
    if (ok) {
        bridge->profile_dirty = false;
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// arg: [name]. Creates a new profile from the current live state and makes
// it active.
static void native_save_profile_as(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    char name[64];
    bool ok = bridge->profiles && parse_string_arg(req, name, sizeof(name)) && name[0] &&
             profile_store_save_as(bridge->profiles, name, bridge->macros, bridge->settings,
                                   bridge->mixer, NULL);
    if (ok) {
        bridge->profile_dirty = false;
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// arg: [profileId, name].
static void native_rename_profile(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long profile_id;
    char name[64];
    bool ok = bridge->profiles && parse_int_and_string_arg(req, &profile_id, name, sizeof(name)) &&
             name[0] && profile_store_rename(bridge->profiles, profile_id, name);
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// arg: [profileId]. Refuses to delete the last remaining profile (see
// profile_store_delete()).
static void native_delete_profile(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[1];
    bool ok = bridge->profiles && parse_int_args(req, args, 1) == 1 &&
             profile_store_delete(bridge->profiles, args[0]);
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// arg: [profileId]. Loads a profile into the live state, discarding any
// unsaved edits to whatever was active -- the UI is expected to confirm
// with the user first if profileDirty is set, this just does the swap.
static void native_switch_profile(const char *id, const char *req, void *arg)
{
    ui_bridge_t *bridge = arg;
    long args[1];
    bool ok = bridge->profiles && parse_int_args(req, args, 1) == 1 &&
             profile_store_load(bridge->profiles, args[0], bridge->macros, bridge->settings,
                                bridge->mixer);
    if (ok) {
        bridge->profile_dirty = false;
    }
    webview_return(bridge->w, id, 0, ok ? "true" : "false");
}

// --- window bootstrap ----------------------------------------------------

static bool get_executable_dir(char *out, size_t out_size)
{
#if defined(_WIN32)
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
    if (len == 0 || len == sizeof(path)) {
        return false;
    }
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        return false;
    }
#else
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len < 0) {
        return false;
    }
    path[len] = '\0';
#endif

    char *slash = strrchr(path, PATH_SEP_CHAR);
    if (!slash) {
        return false;
    }
    *slash = '\0';
    snprintf(out, out_size, "%s", path);
    return true;
}

// Percent-encodes spaces only -- the one character likely to show up in
// real install paths that would otherwise break the file:// URI.
static void append_url_escaped(strbuf_t *sb, const char *s)
{
    for (const char *p = s; *p; p++) {
        if (*p == ' ') {
            sb_appendf(sb, "%%20");
        } else if (*p == '\\') {
            sb_appendf(sb, "/");
        } else {
            sb_appendf(sb, "%c", *p);
        }
    }
}

static bool resolve_ui_index_url(char *out, size_t out_size)
{
    char dir[900];
    if (!get_executable_dir(dir, sizeof(dir))) {
        return false;
    }

    strbuf_t sb;
    sb_init(&sb, out, out_size);
#ifdef _WIN32
    // file:///C:/path/to/dir/ui/index.html
    sb_appendf(&sb, "file:///");
#else
    sb_appendf(&sb, "file://");
#endif
    append_url_escaped(&sb, dir);
    sb_appendf(&sb, "/ui/index.html");
    return true;
}

ui_bridge_t *ui_bridge_create(mixer_state_t *mixer, macro_map_t *macros,
                              device_settings_t *settings, profile_store_t *profiles)
{
    ui_bridge_t *bridge = calloc(1, sizeof(ui_bridge_t));
    if (!bridge) {
        return NULL;
    }
    bridge->mixer = mixer;
    bridge->macros = macros;
    bridge->settings = settings;
    bridge->profiles = profiles;

    bridge->w = webview_create(0, NULL);
    if (!bridge->w) {
        free(bridge);
        return NULL;
    }

    webview_set_title(bridge->w, "TinyPad");
    webview_set_size(bridge->w, 960, 640, WEBVIEW_HINT_NONE);

    webview_bind(bridge->w, "native_assign_slot", native_assign_slot, bridge);
    webview_bind(bridge->w, "native_clear_slot", native_clear_slot, bridge);
    webview_bind(bridge->w, "native_set_slot_volume", native_set_slot_volume, bridge);
    webview_bind(bridge->w, "native_toggle_slot_mute", native_toggle_slot_mute, bridge);
    webview_bind(bridge->w, "native_set_macro", native_set_macro, bridge);
    webview_bind(bridge->w, "native_set_macro_label", native_set_macro_label, bridge);
    webview_bind(bridge->w, "native_set_gui_layout", native_set_gui_layout, bridge);
    webview_bind(bridge->w, "native_set_simulation_enabled", native_set_simulation_enabled, bridge);
    webview_bind(bridge->w, "native_set_simulated_level", native_set_simulated_level, bridge);
    webview_bind(bridge->w, "native_save_profile", native_save_profile, bridge);
    webview_bind(bridge->w, "native_save_profile_as", native_save_profile_as, bridge);
    webview_bind(bridge->w, "native_rename_profile", native_rename_profile, bridge);
    webview_bind(bridge->w, "native_delete_profile", native_delete_profile, bridge);
    webview_bind(bridge->w, "native_switch_profile", native_switch_profile, bridge);

    char url[1024];
    if (!resolve_ui_index_url(url, sizeof(url))) {
        webview_destroy(bridge->w);
        free(bridge);
        return NULL;
    }
    webview_navigate(bridge->w, url);

    return bridge;
}

void ui_bridge_destroy(ui_bridge_t *bridge)
{
    if (!bridge) {
        return;
    }
    if (bridge->w) {
        webview_destroy(bridge->w);
    }
    free(bridge);
}

void ui_bridge_run(ui_bridge_t *bridge) { webview_run(bridge->w); }
