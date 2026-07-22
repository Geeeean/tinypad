#include "core/device_discovery.h"
#include <string.h>

bool device_discovery_matches(const serial_port_info_t *info)
{
    if (info->vendor_id != TINYPAD_USB_VENDOR_ID) {
        return false;
    }
    if (info->product[0] != '\0') {
        return strcmp(info->product, TINYPAD_USB_PRODUCT_STRING) == 0;
    }
    return info->product_id == TINYPAD_USB_PRODUCT_ID;
}

int device_discovery_find(const serial_port_info_t *ports, int count)
{
    for (int i = 0; i < count; i++) {
        if (device_discovery_matches(&ports[i])) {
            return i;
        }
    }
    return -1;
}
