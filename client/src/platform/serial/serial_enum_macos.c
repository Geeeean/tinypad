// macOS serial port enumeration via IOKit: finds every IOSerialBSDClient
// service, then walks up its registry-tree parent to the enclosing USB
// device node for the identifying VID/PID/product string.

#include "platform/serial_enum.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <string.h>

// Walks up from a serial service to the nearest ancestor exposing USB
// vendor/product properties (the enclosing IOUSBHostDevice/IOUSBDevice
// node) -- the serial service itself only has the callout/dialin paths.
static void fill_usb_identity(io_service_t serial_service, serial_port_info_t *out)
{
    io_registry_entry_t entry = serial_service;
    IOObjectRetain(entry);

    while (entry != IO_OBJECT_NULL) {
        CFNumberRef vendor_id_ref = (CFNumberRef)IORegistryEntryCreateCFProperty(
            entry, CFSTR("idVendor"), kCFAllocatorDefault, 0);
        CFNumberRef product_id_ref = (CFNumberRef)IORegistryEntryCreateCFProperty(
            entry, CFSTR("idProduct"), kCFAllocatorDefault, 0);

        if (vendor_id_ref && product_id_ref) {
            int32_t vendor_id = 0, product_id = 0;
            CFNumberGetValue(vendor_id_ref, kCFNumberSInt32Type, &vendor_id);
            CFNumberGetValue(product_id_ref, kCFNumberSInt32Type, &product_id);
            out->vendor_id = (uint16_t)vendor_id;
            out->product_id = (uint16_t)product_id;

            CFStringRef product_ref = (CFStringRef)IORegistryEntryCreateCFProperty(
                entry, CFSTR("USB Product Name"), kCFAllocatorDefault, 0);
            if (product_ref) {
                CFStringGetCString(product_ref, out->product, sizeof(out->product),
                                   kCFStringEncodingUTF8);
                CFRelease(product_ref);
            }

            CFRelease(vendor_id_ref);
            CFRelease(product_id_ref);
            IOObjectRelease(entry);
            return;
        }
        if (vendor_id_ref) {
            CFRelease(vendor_id_ref);
        }
        if (product_id_ref) {
            CFRelease(product_id_ref);
        }

        io_registry_entry_t parent = IO_OBJECT_NULL;
        kern_return_t kr = IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent);
        IOObjectRelease(entry);
        if (kr != KERN_SUCCESS) {
            return;
        }
        entry = parent;
    }
}

int serial_port_list(serial_port_info_t *out, int max)
{
    CFMutableDictionaryRef matching = IOServiceMatching(kIOSerialBSDServiceValue);
    if (!matching) {
        return -1;
    }

    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS) {
        return -1;
    }

    int found = 0;
    io_service_t service;
    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        CFStringRef path_ref = (CFStringRef)IORegistryEntryCreateCFProperty(
            service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);

        if (path_ref) {
            if (found < max) {
                serial_port_info_t *info = &out[found];
                memset(info, 0, sizeof(*info));
                CFStringGetCString(path_ref, info->path, sizeof(info->path),
                                   kCFStringEncodingUTF8);
                fill_usb_identity(service, info);
            }
            found++;
            CFRelease(path_ref);
        }

        IOObjectRelease(service);
    }
    IOObjectRelease(iter);

    return found;
}
