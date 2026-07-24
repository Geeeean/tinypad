#include "core/device_settings.h"
#include "platform/mutex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct device_settings {
    mutex_t lock;
    char macro_labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN];
    uint8_t gui_layout[GUI_COMPONENT_COUNT];
    uint8_t topbar_items[TOPBAR_SLOT_COUNT];
    char profile_name[PROFILE_NAME_WIRE_LEN];
};

device_settings_t *device_settings_create(void)
{
    device_settings_t *settings = calloc(1, sizeof(device_settings_t));
    if (!settings) {
        return NULL;
    }
    mutex_init(&settings->lock);
    // All three pieces on, original order. calloc's zero-init isn't enough
    // here: 0 is GUI_COMPONENT_VU_METERS, not "unset" (GUI_COMPONENT_NONE is
    // 0xFF), so this has to be set explicitly.
    settings->gui_layout[0] = GUI_COMPONENT_VU_METERS;
    settings->gui_layout[1] = GUI_COMPONENT_WAVEFORM;
    settings->gui_layout[2] = GUI_COMPONENT_MACRO_GRID;
    settings->gui_layout[3] = GUI_COMPONENT_NONE; // Channel Rows: opt-in, not on by default
    // Same reasoning as gui_layout above: 0 is TOPBAR_ITEM_CONNECTION, not
    // "unset" (TOPBAR_ITEM_NONE is 0xFF), so calloc's zero-init isn't enough.
    // Empty by default -- off until the user configures it.
    memset(settings->topbar_items, TOPBAR_ITEM_NONE, sizeof(settings->topbar_items));
    return settings;
}

void device_settings_destroy(device_settings_t *settings)
{
    if (!settings) {
        return;
    }
    mutex_destroy(&settings->lock);
    free(settings);
}

void device_settings_set_macro_label(device_settings_t *settings, int index, const char *label)
{
    if (index < 0 || index >= MACRO_BUTTON_COUNT) {
        return;
    }
    mutex_lock(&settings->lock);
    memset(settings->macro_labels[index], 0, MACRO_LABEL_LEN);
    strncpy(settings->macro_labels[index], label, MACRO_LABEL_LEN - 1);
    mutex_unlock(&settings->lock);
}

void device_settings_get_macro_label(device_settings_t *settings, int index, char *out,
                                     size_t out_size)
{
    if (index < 0 || index >= MACRO_BUTTON_COUNT || out_size == 0) {
        return;
    }
    mutex_lock(&settings->lock);
    snprintf(out, out_size, "%s", settings->macro_labels[index]);
    mutex_unlock(&settings->lock);
}

void device_settings_set_gui_layout(device_settings_t *settings,
                                    const uint8_t layout[GUI_COMPONENT_COUNT])
{
    mutex_lock(&settings->lock);
    memcpy(settings->gui_layout, layout, sizeof(settings->gui_layout));
    protocol_normalize_gui_layout(settings->gui_layout);
    mutex_unlock(&settings->lock);
}

void device_settings_get_gui_layout(device_settings_t *settings, uint8_t out[GUI_COMPONENT_COUNT])
{
    mutex_lock(&settings->lock);
    memcpy(out, settings->gui_layout, sizeof(settings->gui_layout));
    mutex_unlock(&settings->lock);
}

void device_settings_set_topbar_items(device_settings_t *settings,
                                      const uint8_t items[TOPBAR_SLOT_COUNT])
{
    mutex_lock(&settings->lock);
    memcpy(settings->topbar_items, items, sizeof(settings->topbar_items));
    protocol_normalize_topbar_items(settings->topbar_items);
    mutex_unlock(&settings->lock);
}

void device_settings_get_topbar_items(device_settings_t *settings,
                                      uint8_t out[TOPBAR_SLOT_COUNT])
{
    mutex_lock(&settings->lock);
    memcpy(out, settings->topbar_items, sizeof(settings->topbar_items));
    mutex_unlock(&settings->lock);
}

void device_settings_set_profile_name(device_settings_t *settings, const char *name)
{
    mutex_lock(&settings->lock);
    memset(settings->profile_name, 0, PROFILE_NAME_WIRE_LEN);
    strncpy(settings->profile_name, name, PROFILE_NAME_WIRE_LEN - 1);
    mutex_unlock(&settings->lock);
}

void device_settings_get_profile_name(device_settings_t *settings, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    mutex_lock(&settings->lock);
    snprintf(out, out_size, "%s", settings->profile_name);
    mutex_unlock(&settings->lock);
}

void device_settings_build_packet(device_settings_t *settings, device_config_packet *out)
{
    char labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN];
    uint8_t gui_layout[GUI_COMPONENT_COUNT];
    uint8_t topbar_items[TOPBAR_SLOT_COUNT];
    char profile_name[PROFILE_NAME_WIRE_LEN];

    mutex_lock(&settings->lock);
    memcpy(labels, settings->macro_labels, sizeof(labels));
    memcpy(gui_layout, settings->gui_layout, sizeof(gui_layout));
    memcpy(topbar_items, settings->topbar_items, sizeof(topbar_items));
    memcpy(profile_name, settings->profile_name, sizeof(profile_name));
    mutex_unlock(&settings->lock);

    protocol_build_device_config_packet(out, labels, gui_layout, topbar_items, profile_name);
}
