#include "core/device_discovery.h"
#include "core/device_link.h"
#include "core/device_settings.h"
#include "core/macro_map.h"
#include "core/mixer_state.h"
#include "core/profile_store.h"
#include "platform/audio_backend.h"
#include "platform/com_init.h"
#include "platform/serial_enum.h"
#include "platform/serial_port.h"
#include "platform/thread.h"
#include "platform/tray.h"
#include "ui/ui_bridge.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

// How often background_loop retries opening the device while none is
// connected -- fast enough to feel responsive on plug-in, slow enough to
// stay negligible CPU for what's meant to be a lightweight background daemon.
#define DEVICE_SCAN_INTERVAL_MS 1500

typedef struct {
    mixer_state_t *mixer;
    macro_map_t *macros;
    device_settings_t *settings;
    device_link_t *device_link; // NULL if no device is attached; background-thread-owned
    // NULL while backgrounded (no window open); set/cleared by the main
    // thread, read by the background thread every tick.
    _Atomic(ui_bridge_t *) bridge;
    atomic_bool running; // false only once Quit is chosen; stops background_loop entirely
    const char *device_path; // manual override (argv[1]/TINYPAD_DEVICE), or NULL to auto-discover
    uint64_t next_scan_at_ms; // background-thread-owned
} app_context_t;

// Opens the configured device path, or auto-discovers the TinyPad by its
// USB identity if none was given. Leaves ctx->device_link NULL on any
// failure -- the caller just retries on the next scan.
static void try_connect_device(app_context_t *ctx)
{
    serial_port_t *port = NULL;

    if (ctx->device_path) {
        port = serial_port_open(ctx->device_path);
        if (!port) {
            return;
        }
    } else {
        serial_port_info_t candidates[16];
        int n = serial_port_list(candidates, 16);
        if (n <= 0) {
            return;
        }

        int available = n < 16 ? n : 16;
        int found = device_discovery_find(candidates, available);
        if (found < 0) {
            return;
        }

        port = serial_port_open(candidates[found].path);
        if (!port) {
            fprintf(stderr, "tinypad: found device at '%s' but failed to open it\n",
                    candidates[found].path);
            return;
        }
        fprintf(stderr, "tinypad: auto-connected to '%s'\n", candidates[found].path);
    }

    ctx->device_link = device_link_create(port, ctx->mixer, ctx->macros, ctx->settings);
    if (!ctx->device_link) {
        serial_port_close(port);
    }
}

static void background_loop(void *arg)
{
    app_context_t *ctx = arg;

    while (atomic_load(&ctx->running)) {
        mixer_state_poll(ctx->mixer);

        if (ctx->device_link && !device_link_poll(ctx->device_link)) {
            fprintf(stderr, "tinypad: device disconnected\n");
            device_link_destroy(ctx->device_link);
            ctx->device_link = NULL;
        }

        if (!ctx->device_link) {
            uint64_t now = monotonic_ms();
            if (now >= ctx->next_scan_at_ms) {
                try_connect_device(ctx);
                ctx->next_scan_at_ms = now + DEVICE_SCAN_INTERVAL_MS;
            }
        }

        ui_bridge_t *bridge = atomic_load(&ctx->bridge);
        if (bridge) {
            ui_bridge_push_state(bridge);
        }
        sleep_ms(16); // ~60Hz
    }
}

int main(int argc, char **argv)
{
    // Must happen before anything that might touch COM itself (WASAPI via
    // mixer_state_create() below, webview internally via ui_bridge_create())
    // -- see platform/com_init.h for why order matters here.
    platform_com_init();

    const char *device_path = argc > 1 ? argv[1] : getenv("TINYPAD_DEVICE");
    if (!device_path) {
        fprintf(stderr, "tinypad: no device path given -- auto-discovering by USB identity\n");
    }

    macro_map_t *macros = macro_map_create();
    mixer_state_t *mixer = mixer_state_create(audio_backend_get_vtable());
    device_settings_t *settings = device_settings_create();
    if (!macros || !mixer || !settings) {
        fprintf(stderr, "tinypad: failed to initialize core state\n");
        return 1;
    }

    // Overlays the active profile's saved macros/settings/slot assignments
    // onto the in-code defaults just set above. Graceful degradation if this
    // fails for any reason (no write access, corrupt db, etc): the app keeps
    // running on those defaults, same as a missing serial device below.
    char db_path[1024];
    profile_store_t *profiles =
        profile_store_default_path(db_path, sizeof(db_path)) ? profile_store_open(db_path) : NULL;
    if (!profiles) {
        fprintf(stderr, "tinypad: could not open profile database (continuing without persistence)\n");
    } else {
        int64_t active_profile_id;
        if (profile_store_get_active_id(profiles, &active_profile_id)) {
            profile_store_load(profiles, active_profile_id, macros, settings, mixer);
        }
    }

    app_context_t ctx = {.mixer = mixer,
                         .macros = macros,
                         .settings = settings,
                         .device_link = NULL,
                         .device_path = device_path,
                         .next_scan_at_ms = 0};
    atomic_init(&ctx.bridge, NULL);
    atomic_store(&ctx.running, true);

    int exit_code = 1;

    thread_t worker;
    if (!thread_start(&worker, background_loop, &ctx)) {
        fprintf(stderr, "tinypad: failed to start background polling thread\n");
        goto cleanup;
    }

    // Loops between showing the window and blocking on the tray icon while
    // it's closed, so closing the window backgrounds the app (freeing the
    // webview/JS engine's memory) instead of exiting the process. The
    // background thread above spans the whole loop, so the mixer and device
    // link keep working the entire time, window open or not.
    for (;;) {
        ui_bridge_t *bridge = ui_bridge_create(mixer, macros, settings, profiles);
        if (bridge) {
            atomic_store(&ctx.bridge, bridge);
            ui_bridge_run(bridge); // blocks on this (main) thread until the window closes
            atomic_store(&ctx.bridge, NULL);
            ui_bridge_destroy(bridge); // frees webview/JS engine memory now
        } else {
            fprintf(stderr, "tinypad: failed to create UI window (continuing in background)\n");
        }

        tray_result_t choice = tray_run(); // blocks this thread until Show/Quit
        if (choice != TRAY_RESULT_SHOW) {
            // TRAY_RESULT_QUIT, or TRAY_RESULT_ERROR (no usable tray means no
            // way back to the UI -- treat it like an explicit quit rather
            // than leaving an invisible, unrecoverable process behind).
            break;
        }
    }

    atomic_store(&ctx.running, false);
    thread_join(worker);
    exit_code = 0;

cleanup:
    if (ctx.device_link) {
        device_link_destroy(ctx.device_link);
    }
    mixer_state_destroy(mixer);
    macro_map_destroy(macros);
    device_settings_destroy(settings);
    profile_store_close(profiles);
    platform_com_uninit();

    return exit_code;
}
