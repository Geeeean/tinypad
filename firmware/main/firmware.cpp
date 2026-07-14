#include "display.hpp"
#include "freertos/idf_additions.h"
#include "gui.hpp"
#include "input_manager.hpp"
#include "usb_manager.hpp"
#include <stdio.h>

static levels_packet shared_levels;
static metadata_packet shared_metadata;
static SemaphoreHandle_t shared_mutex = nullptr;
static USBManager::Config shared_config;

static Display display;
static USBManager usb_manager;
static GUI gui;
static InputManager input_manager;

extern "C" void app_main(void)
{
    display.init();

    shared_mutex = xSemaphoreCreateMutex();
    if (shared_mutex == nullptr) {
        return;
    }

    shared_config.shared_levels = &shared_levels;
    shared_config.shared_metadata = &shared_metadata;
    shared_config.mutex = shared_mutex;

    // usb_manager.init() installs the TinyUSB driver; input_manager's task
    // writes CDC packets, so it must start only after that driver exists.
    usb_manager.init(shared_config);
    gui.init(shared_config);
    input_manager.init();

    gui.start();
    usb_manager.start();
    input_manager.start();

    while (true) {
        printf("CIAO\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
