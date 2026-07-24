#pragma once

// Display-facing settings the host pushes to the device: the 8 switch
// button labels and which GUI dashboard pieces are enabled, and in what
// order. Separate from macro_map (which owns *behavior*, not labels) and
// mixer_state (which owns audio, not display prefs) -- this is the thing
// device_link snapshots into a device_config_packet, and what ui_bridge
// reads/writes from the UI.

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

// layout: GUI_COMPONENT_COUNT ids in draw order (top-to-bottom); a slot
// holding GUI_COMPONENT_NONE disables that piece. Normalized (out-of-range
// ids and duplicates cleared to GUI_COMPONENT_NONE) on every set and again
// when the packet is built, so callers never need to pre-validate it.
void device_settings_set_gui_layout(device_settings_t *settings,
                                    const uint8_t layout[GUI_COMPONENT_COUNT]);
void device_settings_get_gui_layout(device_settings_t *settings,
                                    uint8_t out[GUI_COMPONENT_COUNT]);

// items: TOPBAR_SLOT_COUNT ids, one per fixed topbar position;
// TOPBAR_ITEM_NONE disables that slot. Normalized (out-of-range ids cleared
// to TOPBAR_ITEM_NONE; duplicates are allowed, unlike gui_layout) on every
// set and again when the packet is built.
void device_settings_set_topbar_items(device_settings_t *settings,
                                      const uint8_t items[TOPBAR_SLOT_COUNT]);
void device_settings_get_topbar_items(device_settings_t *settings,
                                      uint8_t out[TOPBAR_SLOT_COUNT]);

// The active profile's display name, shown by the topbar's PROFILE_NAME
// item. Truncated to PROFILE_NAME_WIRE_LEN-1 chars on set.
void device_settings_set_profile_name(device_settings_t *settings, const char *name);
void device_settings_get_profile_name(device_settings_t *settings, char *out, size_t out_size);

// Snapshots current settings into a ready-to-send packet.
void device_settings_build_packet(device_settings_t *settings, device_config_packet *out);
