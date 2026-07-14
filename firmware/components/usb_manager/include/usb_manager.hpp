#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "protocol.hpp"

class USBManager {
  public:
    typedef struct {
        levels_packet *shared_levels;
        metadata_packet *shared_metadata;
        device_config_packet *shared_device_config;
        SemaphoreHandle_t mutex;
    } Config;

    void init(Config config);
    void start();

  private:
    static void usb_task(void *pvParameters);
    bool receive_data(levels_packet *out_levels, metadata_packet *out_metadata,
                      device_config_packet *out_device_config, PacketType *out_type);

    Config _config;

    // Byte-stream framing state, persisted across receive_data() calls so a
    // packet whose body hasn't fully arrived yet is resumed instead of
    // re-read; a bad sync byte only drops that one byte, not a whole frame.
    // Shared with the client's device_link, which runs the same state
    // machine in the other direction (see shared/protocol.h).
    protocol_reader_t _reader{};
};
