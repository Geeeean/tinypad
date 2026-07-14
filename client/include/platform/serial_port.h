#pragma once

// Minimal cross-platform serial (USB CDC-ACM) transport. One implementation
// is compiled per OS: src/platform/serial/serial_posix.c on Linux/macOS,
// src/platform/serial/serial_windows.c on Windows. Both provide exactly
// this API, selected by CMakeLists.txt -- callers never need #ifdef.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// CDC-ACM ignores the baud rate electrically, but host serial APIs still
// require one to be set.
#define SERIAL_BAUD_RATE 115200

typedef struct serial_port serial_port_t;

// Opens the device at `path` (e.g. "/dev/ttyACM0", "/dev/cu.usbmodemXXXX",
// or "COM5" / "\\\\.\\COM5" on Windows). Returns NULL on failure.
serial_port_t *serial_port_open(const char *path);

void serial_port_close(serial_port_t *port);

// Non-blocking read: returns the number of bytes actually read (0 if none
// available right now), or -1 on a fatal I/O error (e.g. device unplugged)
// -- the caller should close the port and try to reopen later.
int serial_port_read(serial_port_t *port, uint8_t *buffer, size_t size);

// Writes the full buffer. Returns false on a fatal I/O error.
bool serial_port_write(serial_port_t *port, const uint8_t *data, size_t size);
