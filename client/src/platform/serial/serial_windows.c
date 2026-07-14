// Win32 COM-port backend for serial_port.h. Not build-verified on this
// (non-Windows) development machine -- review carefully against real
// hardware before relying on it.

#include "platform/serial_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

struct serial_port {
    HANDLE handle;
};

serial_port_t *serial_port_open(const char *path)
{
    // COM10 and above require the \\.\ prefix; using it unconditionally
    // also works for COM1-9, so normalize every path through it.
    char full_path[260];
    if (strncmp(path, "\\\\.\\", 4) == 0) {
        snprintf(full_path, sizeof(full_path), "%s", path);
    } else {
        snprintf(full_path, sizeof(full_path), "\\\\.\\%s", path);
    }

    HANDLE handle = CreateFileA(full_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                0, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) {
        CloseHandle(handle);
        return NULL;
    }
    dcb.BaudRate = SERIAL_BAUD_RATE;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(handle, &dcb)) {
        CloseHandle(handle);
        return NULL;
    }

    // ReadIntervalTimeout = MAXDWORD with the multiplier/constant at 0 makes
    // ReadFile return immediately with whatever is already buffered (even 0
    // bytes) instead of blocking -- matches serial_port_read()'s contract.
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 200; // bounded stall, mirrors the POSIX backend
    if (!SetCommTimeouts(handle, &timeouts)) {
        CloseHandle(handle);
        return NULL;
    }

    serial_port_t *port = malloc(sizeof(serial_port_t));
    if (!port) {
        CloseHandle(handle);
        return NULL;
    }
    port->handle = handle;
    return port;
}

void serial_port_close(serial_port_t *port)
{
    if (!port) {
        return;
    }
    CloseHandle(port->handle);
    free(port);
}

int serial_port_read(serial_port_t *port, uint8_t *buffer, size_t size)
{
    DWORD read_bytes = 0;
    if (!ReadFile(port->handle, buffer, (DWORD)size, &read_bytes, NULL)) {
        return -1;
    }
    return (int)read_bytes;
}

bool serial_port_write(serial_port_t *port, const uint8_t *data, size_t size)
{
    size_t written = 0;
    while (written < size) {
        DWORD chunk_written = 0;
        if (!WriteFile(port->handle, data + written, (DWORD)(size - written), &chunk_written,
                       NULL)) {
            return false;
        }
        if (chunk_written == 0) {
            // WriteTotalTimeoutConstant elapsed without the port accepting
            // anything -- treat like the POSIX backend's stall bailout.
            return false;
        }
        written += chunk_written;
    }
    return true;
}
