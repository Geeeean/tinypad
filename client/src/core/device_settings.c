#include "core/device_settings.h"
#include "platform/mutex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct device_settings {
    mutex_t lock;
    char macro_labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN];
    bool show_graph;
};

device_settings_t *device_settings_create(void)
{
    device_settings_t *settings = calloc(1, sizeof(device_settings_t));
    if (!settings) {
        return NULL;
    }
    mutex_init(&settings->lock);
    settings->show_graph = true;
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

void device_settings_set_show_graph(device_settings_t *settings, bool show)
{
    mutex_lock(&settings->lock);
    settings->show_graph = show;
    mutex_unlock(&settings->lock);
}

bool device_settings_get_show_graph(device_settings_t *settings)
{
    mutex_lock(&settings->lock);
    bool show = settings->show_graph;
    mutex_unlock(&settings->lock);
    return show;
}

void device_settings_build_packet(device_settings_t *settings, device_config_packet *out)
{
    char labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN];
    uint8_t show_graph;

    mutex_lock(&settings->lock);
    memcpy(labels, settings->macro_labels, sizeof(labels));
    show_graph = settings->show_graph ? 1 : 0;
    mutex_unlock(&settings->lock);

    protocol_build_device_config_packet(out, labels, show_graph);
}
