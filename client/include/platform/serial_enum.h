#pragma once

// USB-serial port enumeration, for auto-discovering the TinyPad device
// without a user-supplied path. One implementation compiled per OS,
// mirroring serial_port.h's flat-API pattern:
//   - src/platform/serial/serial_enum_linux.c   (sysfs)
//   - src/platform/serial/serial_enum_macos.c   (IOKit)
//   - src/platform/serial/serial_enum_windows.c (SetupAPI)

#include <stdint.h>

#define SERIAL_PORT_PATH_LEN 256
#define SERIAL_PORT_PRODUCT_LEN 128

typedef struct {
    char path[SERIAL_PORT_PATH_LEN]; // ready to pass to serial_port_open()
    uint16_t vendor_id;
    uint16_t product_id;
    char product[SERIAL_PORT_PRODUCT_LEN]; // may be empty if not cheaply available
} serial_port_info_t;

// Fills `out` (capacity `max`) with currently-present candidate serial
// ports. Returns the number found (may exceed `max`, snprintf-style, so
// truncation is detectable), or -1 on an enumeration-level failure. Cheap
// enough for a ~1-2s polling cadence; not for a tight loop.
int serial_port_list(serial_port_info_t *out, int max);
