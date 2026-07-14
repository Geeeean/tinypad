#include "core/device_link.h"
#include "core/device_settings.h"
#include "core/macro_map.h"
#include "core/mixer_state.h"
#include "platform/audio_backend.h"
#include "platform/com_init.h"
#include "platform/serial_port.h"
#include "platform/thread.h"
#include "ui/ui_bridge.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    mixer_state_t *mixer;
    device_link_t *device_link; // NULL if no device is attached; background-thread-owned
    ui_bridge_t *bridge;
    atomic_bool running;
} app_context_t;

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

        ui_bridge_push_state(ctx->bridge);
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

    macro_map_t *macros = macro_map_create();
    mixer_state_t *mixer = mixer_state_create(audio_backend_get_vtable());
    device_settings_t *settings = device_settings_create();
    if (!macros || !mixer || !settings) {
        fprintf(stderr, "tinypad: failed to initialize core state\n");
        return 1;
    }

    app_context_t ctx = {.mixer = mixer, .device_link = NULL, .bridge = NULL};
    atomic_store(&ctx.running, true);

    if (device_path) {
        serial_port_t *port = serial_port_open(device_path);
        if (port) {
            ctx.device_link = device_link_create(port, mixer, macros, settings);
        } else {
            fprintf(stderr, "tinypad: could not open device '%s' (continuing without it)\n",
                    device_path);
        }
    } else {
        fprintf(stderr,
                "tinypad: no device configured (pass a serial path as argv[1] or set "
                "TINYPAD_DEVICE) -- running with UI/audio only\n");
    }

    ctx.bridge = ui_bridge_create(mixer, macros, settings);
    if (!ctx.bridge) {
        fprintf(stderr, "tinypad: failed to create UI window\n");
        if (ctx.device_link) {
            device_link_destroy(ctx.device_link);
        }
        mixer_state_destroy(mixer);
        macro_map_destroy(macros);
        device_settings_destroy(settings);
        return 1;
    }

    thread_t worker;
    if (!thread_start(&worker, background_loop, &ctx)) {
        fprintf(stderr, "tinypad: failed to start background polling thread\n");
        ui_bridge_destroy(ctx.bridge);
        if (ctx.device_link) {
            device_link_destroy(ctx.device_link);
        }
        mixer_state_destroy(mixer);
        macro_map_destroy(macros);
        device_settings_destroy(settings);
        return 1;
    }

    ui_bridge_run(ctx.bridge); // blocks on this (main) thread until the window closes

    atomic_store(&ctx.running, false);
    thread_join(worker);

    ui_bridge_destroy(ctx.bridge);
    if (ctx.device_link) {
        device_link_destroy(ctx.device_link);
    }
    mixer_state_destroy(mixer);
    macro_map_destroy(macros);
    device_settings_destroy(settings);
    platform_com_uninit();

    return 0;
}
