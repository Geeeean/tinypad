#pragma once

#include <cstdint>
#define LGFX_USE_V1
#include "protocol.h"
#include "usb_manager.hpp"
#include <LovyanGFX.hpp>

class GUI {
  public:
    void init(USBManager::Config config);

    // Starts the FreeRTOS GUI thread on Core 1
    void start();

  private:
    // Vertical [top, bottom) slice a single dashboard piece draws into for
    // this frame.
    struct Bounds {
        int top;
        int bottom;
    };

    // One slice per GUI_COMPONENT_* id, indexed by id; only entries named by
    // an enabled slot in the frame's gui_layout are meaningful. Stacks
    // enabled components top-to-bottom in gui_layout's order: the two
    // fixed-height pieces (waveform, macro grid) keep their usual size
    // wherever they land in the order, and VU meters -- wherever it lands --
    // takes whatever vertical space is left over.
    struct Layout {
        Bounds bounds[GUI_COMPONENT_COUNT];
    };

    static Layout compute_layout(int canvas_height,
                                 const uint8_t gui_layout[GUI_COMPONENT_COUNT]);

    // The main loop running inside the FreeRTOS task
    static void gui_task(void *pvParameters);

    // Specific layout rendering functions (Drawing into the RAM sprite)
    void draw_vu_meters(lgfx::LGFX_Sprite &canvas, const Bounds &bounds,
                        const channel_level *channels, const metadata_packet &metadata);
    void draw_audio_waveform(lgfx::LGFX_Sprite &canvas, const Bounds &bounds);
    void draw_macro_label(lgfx::LGFX_Sprite &canvas, const Bounds &bounds,
                          const device_config_packet &device_config);

    USBManager::Config _config;
};
