#pragma once

// Native side of the webview UI shell: owns the window, loads ui/index.html
// (shipped next to the executable, see CMakeLists.txt's POST_BUILD copy),
// binds a handful of JS-callable native functions, and periodically pushes
// a state snapshot into the page via window.onState(...).

#include "core/device_settings.h"
#include "core/macro_map.h"
#include "core/mixer_state.h"

typedef struct ui_bridge ui_bridge_t;

ui_bridge_t *ui_bridge_create(mixer_state_t *mixer, macro_map_t *macros,
                              device_settings_t *settings);
void ui_bridge_destroy(ui_bridge_t *bridge);

// Cheap; call from the background polling thread, not the UI thread.
void ui_bridge_push_state(ui_bridge_t *bridge);

// Blocks running the native event loop until the window is closed. Call
// last, from the main thread.
void ui_bridge_run(ui_bridge_t *bridge);
