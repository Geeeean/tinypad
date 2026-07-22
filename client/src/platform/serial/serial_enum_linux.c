// Linux serial port enumeration via sysfs: walks /sys/class/tty/*/device up
// to the enclosing USB device node's idVendor/idProduct/product files. No
// libudev dependency, matching this project's minimal-deps stance.

#include "platform/serial_enum.h"
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool read_trimmed(const char *path, char *out, size_t out_size)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return false;
    }
    bool ok = fgets(out, (int)out_size, f) != NULL;
    fclose(f);
    if (!ok) {
        return false;
    }
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
        out[--len] = '\0';
    }
    return true;
}

// Walks up from a tty's resolved device path (typically the USB interface
// node, e.g. .../1-2:1.0) to the nearest ancestor directory that has
// idVendor -- the enclosing USB device node, usually one level up.
static bool find_usb_ancestor(const char *device_realpath, char *out_dir, size_t out_size)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s", device_realpath);

    for (int depth = 0; depth < 6; depth++) {
        char id_vendor_path[PATH_MAX];
        snprintf(id_vendor_path, sizeof(id_vendor_path), "%s/idVendor", path);
        if (access(id_vendor_path, R_OK) == 0) {
            snprintf(out_dir, out_size, "%s", path);
            return true;
        }
        char *slash = strrchr(path, '/');
        if (!slash || slash == path) {
            return false;
        }
        *slash = '\0';
    }
    return false;
}

int serial_port_list(serial_port_info_t *out, int max)
{
    DIR *dir = opendir("/sys/class/tty");
    if (!dir) {
        return -1;
    }

    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char device_link[PATH_MAX];
        snprintf(device_link, sizeof(device_link), "/sys/class/tty/%s/device", entry->d_name);

        char device_realpath[PATH_MAX];
        if (!realpath(device_link, device_realpath)) {
            continue; // no `device` symlink -- a virtual tty, not a real port
        }

        char usb_dir[PATH_MAX];
        if (!find_usb_ancestor(device_realpath, usb_dir, sizeof(usb_dir))) {
            continue; // not a USB-backed tty (e.g. a platform/legacy serial port)
        }

        char id_vendor_path[PATH_MAX], id_product_path[PATH_MAX], product_path[PATH_MAX];
        snprintf(id_vendor_path, sizeof(id_vendor_path), "%s/idVendor", usb_dir);
        snprintf(id_product_path, sizeof(id_product_path), "%s/idProduct", usb_dir);
        snprintf(product_path, sizeof(product_path), "%s/product", usb_dir);

        char vendor_str[16] = {0}, product_id_str[16] = {0};
        if (!read_trimmed(id_vendor_path, vendor_str, sizeof(vendor_str))) {
            continue;
        }
        read_trimmed(id_product_path, product_id_str, sizeof(product_id_str));

        if (found < max) {
            serial_port_info_t *info = &out[found];
            memset(info, 0, sizeof(*info));
            snprintf(info->path, sizeof(info->path), "/dev/%s", entry->d_name);
            info->vendor_id = (uint16_t)strtol(vendor_str, NULL, 16);
            info->product_id = (uint16_t)strtol(product_id_str, NULL, 16);
            read_trimmed(product_path, info->product, sizeof(info->product));
        }
        found++;
    }
    closedir(dir);

    return found;
}
