#pragma once

// Display-facing settings the host pushes to the device: the 8 switch
// button labels and whether to draw the waveform graph. Separate from
// macro_map (which owns *behavior*, not labels) and mixer_state (which
// owns audio, not display prefs) -- this is the thing device_link snapshots
// into a device_config_packet, and what ui_bridge reads/writes from the UI.

#include "protocol.h"
#include <stdbool.h>

typedef struct device_settings device_settings_t;

device_settings_t *device_settings_create(void);
void device_settings_destroy(device_settings_t *settings);

// index: 0..MACRO_BUTTON_COUNT-1 (the 8 switches only -- encoder buttons
// have no on-device label box). No-op out of range.
void device_settings_set_macro_label(device_settings_t *settings, int index, const char *label);
void device_settings_get_macro_label(device_settings_t *settings, int index, char *out,
                                     size_t out_size);

void device_settings_set_show_graph(device_settings_t *settings, bool show);
bool device_settings_get_show_graph(device_settings_t *settings);

// Snapshots current settings into a ready-to-send packet.
void device_settings_build_packet(device_settings_t *settings, device_config_packet *out);
