#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "protocol.h"

class USBManager {
  public:
    typedef struct {
        levels_packet *shared_levels;
        metadata_packet *shared_metadata;
        device_config_packet *shared_device_config;
        // esp_timer_get_time() at the most recent valid LEVELS packet;
        // lets GUI detect a dead host and fall back to the idle screen.
        int64_t *shared_last_levels_us;
        SemaphoreHandle_t mutex;
    } Config;

    void init(Config config);
    void start();

  private:
    static void usb_task(void *pvParameters);
    bool receive_data(levels_packet *out_levels, metadata_packet *out_metadata,
                      device_config_packet *out_device_config, uint8_t *out_type);

    Config _config;

    // Framing state persisted across receive_data() calls so an in-progress
    // packet is resumed, not re-read; a bad sync byte drops one byte only.
    protocol_reader_t _reader{};
};
