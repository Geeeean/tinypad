#include "display.hpp"
#include "freertos/idf_additions.h"
#include "gui.hpp"
#include "input_manager.hpp"
#include "usb_manager.hpp"

static levels_packet shared_levels;
static metadata_packet shared_metadata;
static device_config_packet shared_device_config;
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
    shared_config.shared_device_config = &shared_device_config;
    shared_config.mutex = shared_mutex;

    usb_manager.init(shared_config);
    gui.init(shared_config);
    input_manager.init();

    gui.start();
    usb_manager.start();
    input_manager.start();

    vTaskDelete(nullptr);
}
