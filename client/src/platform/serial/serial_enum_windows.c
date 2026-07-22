// Windows serial port enumeration via SetupAPI: walks the "Ports" device
// class, reading the hardware ID (for VID/PID) and the "PortName" registry
// value (for the exact COMx name -- serial_port_open() normalizes it with
// the \\.\ prefix itself, so a plain "COM5" is what it expects here). Not
// build-verified in this repo's dev environment (no Windows toolchain
// available) -- same caveat as serial_windows.c.

#include "platform/serial_enum.h"
#include <devguid.h>
#include <initguid.h>
#include <setupapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool parse_vid_pid(const char *hardware_id, uint16_t *vendor_id, uint16_t *product_id)
{
    const char *vid_pos = strstr(hardware_id, "VID_");
    const char *pid_pos = strstr(hardware_id, "PID_");
    if (!vid_pos || !pid_pos) {
        return false;
    }

    unsigned int vid = 0, pid = 0;
    if (sscanf(vid_pos, "VID_%4x", &vid) != 1 || sscanf(pid_pos, "PID_%4x", &pid) != 1) {
        return false;
    }
    *vendor_id = (uint16_t)vid;
    *product_id = (uint16_t)pid;
    return true;
}

int serial_port_list(serial_port_info_t *out, int max)
{
    HDEVINFO dev_info = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (dev_info == INVALID_HANDLE_VALUE) {
        return -1;
    }

    int found = 0;
    SP_DEVINFO_DATA dev_data;
    dev_data.cbSize = sizeof(dev_data);

    for (DWORD index = 0; SetupDiEnumDeviceInfo(dev_info, index, &dev_data); index++) {
        char hardware_id[512] = {0};
        if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_data, SPDRP_HARDWAREID, NULL,
                                               (PBYTE)hardware_id, sizeof(hardware_id), NULL)) {
            continue;
        }

        uint16_t vendor_id, product_id;
        if (!parse_vid_pid(hardware_id, &vendor_id, &product_id)) {
            continue; // not a USB device (e.g. a legacy platform COM port)
        }

        HKEY key =
            SetupDiOpenDevRegKey(dev_info, &dev_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) {
            continue;
        }

        char port_name[64] = {0};
        DWORD port_name_size = sizeof(port_name);
        LONG result =
            RegQueryValueExA(key, "PortName", NULL, NULL, (LPBYTE)port_name, &port_name_size);
        RegCloseKey(key);
        if (result != ERROR_SUCCESS) {
            continue;
        }

        if (found < max) {
            serial_port_info_t *info = &out[found];
            memset(info, 0, sizeof(*info));
            snprintf(info->path, sizeof(info->path), "%s", port_name);
            info->vendor_id = vendor_id;
            info->product_id = product_id;
            // Product string isn't cheaply available via this API; left
            // empty -- device_discovery_matches() falls back to VID+PID.
        }
        found++;
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return found;
}
