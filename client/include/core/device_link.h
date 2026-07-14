#pragma once

// Host-side counterpart to firmware's USBManager: writes levels/metadata
// packets down to the device and decodes command_event packets coming back
// up, using the same shared/protocol.h frame reader firmware uses.

#include "core/device_settings.h"
#include "core/macro_map.h"
#include "core/mixer_state.h"
#include "platform/serial_port.h"
#include <stdbool.h>

typedef struct device_link device_link_t;

// Takes ownership of `port` (closed by device_link_destroy). Does not take
// ownership of `mixer`/`macros`/`settings`, which must outlive the link.
device_link_t *device_link_create(serial_port_t *port, mixer_state_t *mixer, macro_map_t *macros,
                                  device_settings_t *settings);
void device_link_destroy(device_link_t *link);

// Call every main-loop tick: decodes any pending command_event bytes and
// applies them to the mixer, then writes a fresh levels_packet down.
// Metadata and device_config are pushed automatically whenever they've
// changed since the last tick. Returns false if a fatal I/O error was hit
// (device_link is then unusable and should be destroyed; caller decides
// whether/when to reopen the port and create a new link).
bool device_link_poll(device_link_t *link);
