#include "display.hpp"
#include "freertos/idf_additions.h"
#include "gui.hpp"
#include "usb_manager.hpp"
#include <stdio.h>

static mixer_data_in shared_buffer;
static SemaphoreHandle_t shared_mutex = nullptr;
static USBManager::Config shared_config;

static Display display;
static USBManager usb_manager;
static GUI gui;

extern "C" void app_main(void)
{
    display.init();

    shared_mutex = xSemaphoreCreateMutex();
    if (shared_mutex == nullptr) {
        return;
    }

    shared_config.shared_data = &shared_buffer;
    shared_config.mutex = shared_mutex;

    usb_manager.init(shared_config);
    gui.init(shared_config);

    gui.start();
    usb_manager.start();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
