#pragma once

#include <cstdint>
#define LGFX_USE_V1
#include "protocol.hpp"
#include "usb_manager.hpp"
#include <LovyanGFX.hpp>

class GUI {
  public:
    void init(USBManager::Config config);

    // Starts the FreeRTOS GUI thread on Core 1
    void start();

    // Thread-safe method to update mixer data from USB
    static void update_mixer_data(const mixer_data_in &new_data);

  private:
    // The main loop running inside the FreeRTOS task
    static void gui_task(void *pvParameters);

    // Specific layout rendering functions (Drawing into the RAM sprite)
    void draw_top_bar(lgfx::LGFX_Sprite &canvas, int unit);
    void draw_vu_meters(lgfx::LGFX_Sprite &canvas, uint8_t *volumes);
    void draw_system_stats(lgfx::LGFX_Sprite &canvas, int unit);
    void draw_audio_waveform(lgfx::LGFX_Sprite &canvas, int unit);
    void draw_macro_label(lgfx::LGFX_Sprite &canvas, int unit);

    USBManager::Config _config;
};
