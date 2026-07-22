#pragma once

// Pure matching logic for identifying the TinyPad among enumerated serial
// ports (see platform/serial_enum.h) -- no OS dependency, unit-testable.

#include "platform/serial_enum.h"
#include <stdbool.h>

// Fixed USB identity the firmware enumerates as -- see firmware/sdkconfig's
// CONFIG_TINYUSB_DESC_* and usb_descriptors.c's USB_TUSB_PID derivation.
#define TINYPAD_USB_VENDOR_ID 0x303A
#define TINYPAD_USB_PRODUCT_ID 0x4001
#define TINYPAD_USB_PRODUCT_STRING "TinyPad"

// True if `info` looks like a TinyPad device. Primary match is VID +
// product string (robust to the derived PID shifting if firmware's TinyUSB
// class config ever changes, e.g. enabling MSC/HID/MIDI/AUDIO); falls back
// to VID + PID when the product string wasn't cheaply available (some
// platforms expose VID/PID but not the string descriptor without extra work).
bool device_discovery_matches(const serial_port_info_t *info);

// Index of the first match in `ports[0..count)`, or -1 if none.
int device_discovery_find(const serial_port_info_t *ports, int count);
