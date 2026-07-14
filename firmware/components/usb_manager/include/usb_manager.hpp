#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "protocol.hpp"

class USBManager {
  public:
    typedef struct {
        levels_packet *shared_levels;
        metadata_packet *shared_metadata;
        SemaphoreHandle_t mutex;
    } Config;

    void init(Config config);
    void start();

  private:
    // Byte-stream framing state, persisted across receive_data() calls so a
    // packet whose body hasn't fully arrived yet is resumed instead of
    // re-read; a bad sync byte only drops that one byte, not a whole frame.
    enum class RxState { SYNC, TYPE, BODY };

    static void usb_task(void *pvParameters);
    bool receive_data(levels_packet *out_levels, metadata_packet *out_metadata,
                      PacketType *out_type);

    Config _config;

    RxState _rx_state = RxState::SYNC;
    uint8_t _rx_buffer[sizeof(metadata_packet)];
    size_t _rx_filled = 0;
    size_t _rx_target = 0;
};
