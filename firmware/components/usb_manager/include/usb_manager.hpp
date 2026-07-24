#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "protocol.h"
#include <atomic>

class USBManager {
  public:
    typedef struct {
        levels_packet *shared_levels;
        metadata_packet *shared_metadata;
        device_config_packet *shared_device_config;
        // esp_timer_get_time() at the most recent valid LEVELS packet;
        // lets GUI detect a dead host and fall back to the idle screen.
        int64_t *shared_last_levels_us;
        // Per-encoder "is the button currently held" state, written by
        // InputManager on every scan tick and read by GUI for the topbar's
        // fine-step indicator -- local hardware state, not mutex-protected
        // like the packets above (a 1ms-period scan loop and atomics are
        // the natural fit, not a mutex round trip). Indexed 0..MIXER_CHANNELS-1
        // (== InputManager::ENCODER_COUNT, both the device's 4 physical knobs).
        std::atomic<bool> *encoder_btn_held;
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
