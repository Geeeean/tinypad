#include "usb_manager.hpp"
#include "esp_log.h"
#include "tinyusb.h"

static const char *TAG = "USB";

void USBManager::init(Config config)
{
    ESP_LOGI(TAG, "Initializing hardware...");

    _config = config;

    tinyusb_config_t tusb_cfg = {
        .port = TINYUSB_PORT_FULL_SPEED_0,
        .phy = {.skip_setup = false, .self_powered = false, .vbus_monitor_io = -1},
        .task = {.size = 4096, .priority = 5, .xCoreID = 0},
        .descriptor = {.device = nullptr,
                       .qualifier = nullptr,
                       .string = nullptr,
                       .string_count = 0,
                       .full_speed_config = nullptr,
                       .high_speed_config = nullptr},
        .event_cb = nullptr,
        .event_arg = nullptr};

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver installation failed: %s", esp_err_to_name(ret));
    }
}

void USBManager::start()
{
    ESP_LOGI(TAG, "Spawning processing worker thread...");

    // Pass 'this' (the pointer to this unique USBManager instance)
    // as the pvParameters block so the static task can find its configuration data.
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        USBManager::usb_task, "usb_manager_task", 4096, this, 5, nullptr, 0);

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to spin up processing task!");
    }
}

void USBManager::usb_task(void *pvParameters)
{
    ESP_LOGI(TAG, "USB Thread alive.");

    USBManager *instance = static_cast<USBManager *>(pvParameters);

    mixer_data_in incoming_packet;

    while (true) {
        if (instance->receive_data(&incoming_packet)) {
            SemaphoreHandle_t mtx = instance->_config.mutex;
            mixer_data_in *shared = instance->_config.shared_data;

            if (mtx != nullptr && xSemaphoreTake(mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
                *shared = incoming_packet;
                xSemaphoreGive(mtx);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

bool USBManager::receive_data(mixer_data_in *out_data)
{
    const size_t expected_size = sizeof(mixer_data_in);

    // Check if there are enough bytes waiting in the hardware CDC buffer (Port 0)
    if (tud_cdc_n_available(0) < expected_size) {
        return false;
    }

    uint8_t raw_buffer[expected_size];
    uint32_t bytes_read = tud_cdc_n_read(0, raw_buffer, expected_size);

    if (bytes_read != expected_size) {
        return false;
    }

    if (!Protocol::parse_packet(raw_buffer, expected_size, out_data)) {
        ESP_LOGW(TAG, "USB packet discarded: bad header or checksum");
        return false;
    }

    return true;
}
