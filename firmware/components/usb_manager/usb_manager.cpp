#include "usb_manager.hpp"
#include "esp_log.h"
#include "tinyusb.h"
#include <algorithm>

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

    levels_packet incoming_levels;
    metadata_packet incoming_metadata;
    PacketType incoming_type;

    while (true) {
        if (instance->receive_data(&incoming_levels, &incoming_metadata, &incoming_type)) {
            SemaphoreHandle_t mtx = instance->_config.mutex;

            if (mtx != nullptr && xSemaphoreTake(mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (incoming_type == PacketType::LEVELS &&
                    instance->_config.shared_levels != nullptr) {
                    *instance->_config.shared_levels = incoming_levels;
                } else if (incoming_type == PacketType::METADATA &&
                          instance->_config.shared_metadata != nullptr) {
                    *instance->_config.shared_metadata = incoming_metadata;
                }
                xSemaphoreGive(mtx);
            }
        }

        vTaskDelay(1);
    }
}

bool USBManager::receive_data(levels_packet *out_levels, metadata_packet *out_metadata,
                              PacketType *out_type)
{
    while (true) {
        uint32_t available = tud_cdc_n_available(0);
        if (available == 0) {
            break;
        }

        switch (_rx_state) {
        case RxState::SYNC: {
            uint8_t byte;
            tud_cdc_n_read(0, &byte, 1);
            if (byte == PROTOCOL_START_BYTE) {
                _rx_buffer[0] = byte;
                _rx_filled = 1;
                _rx_state = RxState::TYPE;
            }
            break;
        }

        case RxState::TYPE: {
            uint8_t byte;
            tud_cdc_n_read(0, &byte, 1);
            _rx_buffer[1] = byte;
            _rx_filled = 2;

            if (!Protocol::size_for_type(byte, &_rx_target)) {
                ESP_LOGW(TAG, "USB packet discarded: unknown packet type 0x%02X", byte);
                _rx_state = RxState::SYNC;
                _rx_filled = 0;
                break;
            }

            _rx_state = RxState::BODY;
            break;
        }

        case RxState::BODY: {
            size_t remaining = _rx_target - _rx_filled;
            size_t to_read = std::min<size_t>(remaining, available);
            uint32_t bytes_read = tud_cdc_n_read(0, &_rx_buffer[_rx_filled], to_read);
            _rx_filled += bytes_read;

            if (_rx_filled == _rx_target) {
                PacketType type = static_cast<PacketType>(_rx_buffer[1]);
                bool ok = false;

                if (type == PacketType::LEVELS) {
                    ok = Protocol::parse_levels_packet(_rx_buffer, _rx_target, out_levels);
                } else if (type == PacketType::METADATA) {
                    ok = Protocol::parse_metadata_packet(_rx_buffer, _rx_target, out_metadata);
                }

                _rx_state = RxState::SYNC;
                _rx_filled = 0;

                if (ok) {
                    *out_type = type;
                    return true;
                }

                ESP_LOGW(TAG, "USB packet discarded: bad checksum");
            }
            break;
        }
        }
    }

    return false;
}
