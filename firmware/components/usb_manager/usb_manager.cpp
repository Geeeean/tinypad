#include "usb_manager.hpp"
#include "esp_log.h"
#include "tinyusb.h"

static const char *TAG = "USB";

void USBManager::init(Config config)
{
    ESP_LOGI(TAG, "Initializing hardware...");

    _config = config;
    protocol_reader_init(&_reader);

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
    device_config_packet incoming_device_config;
    PacketType incoming_type;

    while (true) {
        if (instance->receive_data(&incoming_levels, &incoming_metadata, &incoming_device_config,
                                   &incoming_type)) {
            SemaphoreHandle_t mtx = instance->_config.mutex;

            if (mtx != nullptr && xSemaphoreTake(mtx, pdMS_TO_TICKS(5)) == pdTRUE) {
                if (incoming_type == PacketType::LEVELS &&
                    instance->_config.shared_levels != nullptr) {
                    *instance->_config.shared_levels = incoming_levels;
                } else if (incoming_type == PacketType::METADATA &&
                          instance->_config.shared_metadata != nullptr) {
                    *instance->_config.shared_metadata = incoming_metadata;
                } else if (incoming_type == PacketType::DEVICE_CONFIG &&
                          instance->_config.shared_device_config != nullptr) {
                    *instance->_config.shared_device_config = incoming_device_config;
                }
                xSemaphoreGive(mtx);
            }
        }

        vTaskDelay(1);
    }
}

bool USBManager::receive_data(levels_packet *out_levels, metadata_packet *out_metadata,
                              device_config_packet *out_device_config, PacketType *out_type)
{
    uint8_t byte;
    while (tud_cdc_n_read(0, &byte, 1) == 1) {
        uint8_t type;
        size_t size;
        protocol_feed_result_t result = protocol_reader_feed(&_reader, byte, &type, &size);

        if (result == PROTOCOL_FEED_INCOMPLETE) {
            continue;
        }

        if (result == PROTOCOL_FEED_BAD_TYPE) {
            ESP_LOGW(TAG, "USB packet discarded: unknown packet type 0x%02X", byte);
            continue;
        }

        if (result == PROTOCOL_FEED_BAD_CHECKSUM) {
            ESP_LOGW(TAG, "USB packet discarded: bad checksum");
            continue;
        }

        // PROTOCOL_FEED_COMPLETE: reader->buffer[0..size) holds a validated
        // packet. protocol_reader_feed() already reset the framing state for
        // the next call, but leaves the buffer contents intact for us here.
        PacketType packet_type = static_cast<PacketType>(type);
        bool ok = false;

        if (packet_type == PacketType::LEVELS) {
            ok = Protocol::parse_levels_packet(_reader.buffer, size, out_levels);
        } else if (packet_type == PacketType::METADATA) {
            ok = Protocol::parse_metadata_packet(_reader.buffer, size, out_metadata);
        } else if (packet_type == PacketType::DEVICE_CONFIG) {
            ok = Protocol::parse_device_config_packet(_reader.buffer, size, out_device_config);
        }

        if (ok) {
            *out_type = packet_type;
            return true;
        }
    }

    return false;
}
