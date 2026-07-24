#pragma once

#include <cstdint>
#define LGFX_USE_V1
#include "protocol.h"
#include "usb_manager.hpp"
#include <LovyanGFX.hpp>
#include <string>

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

    // One slice per GUI_COMPONENT_* id; only entries named by an enabled
    // gui_layout slot are meaningful. VU meters take whatever space is left.
    struct Layout {
        Bounds bounds[GUI_COMPONENT_COUNT];
    };

    // top_offset is where the stack starts (PADDING with no topbar, or
    // just past the topbar strip when one is enabled) -- the space between
    // canvas_height and the bottom of the last component is still PADDING.
    // assigned_channels is how many of the 4 knobs currently have a session
    // assigned -- only channel rows' reserved height depends on it, so an
    // unassigned knob collapses its row instead of leaving a blank gap.
    static Layout compute_layout(int canvas_height, int top_offset,
                                 const uint8_t gui_layout[GUI_COMPONENT_COUNT],
                                 int assigned_channels);

    // The main loop running inside the FreeRTOS task
    static void gui_task(void *pvParameters);

    // Specific layout rendering functions (Drawing into the RAM sprite)
    void draw_vu_meters(lgfx::LGFX_Sprite &canvas, const Bounds &bounds,
                        const channel_level *channels, const metadata_packet &metadata);
    void draw_audio_waveform(lgfx::LGFX_Sprite &canvas, const Bounds &bounds);
    void draw_macro_label(lgfx::LGFX_Sprite &canvas, const Bounds &bounds,
                          const device_config_packet &device_config);
    void draw_channel_rows(lgfx::LGFX_Sprite &canvas, const Bounds &bounds,
                           const channel_level *channels, const metadata_packet &metadata);

    // Persistent horizontal strip above whatever the layout above draws --
    // always TOPBAR_HEIGHT tall, 3 fixed cells, one TOPBAR_ITEM_* token each.
    // output_level is get_smoothed_output_level()'s already-computed result
    // (same call gui_task makes for the waveform graph), passed in so
    // TOPBAR_ITEM_OUTPUT_LEVEL matches what the graph shows instead of
    // recomputing (and re-smoothing) it separately.
    void draw_topbar(lgfx::LGFX_Sprite &canvas, const levels_packet &levels,
                     const device_config_packet &device_config, bool is_woke_up,
                     int output_level);
    std::string topbar_item_text(uint8_t item, const levels_packet &levels,
                                 const device_config_packet &device_config, bool is_woke_up,
                                 int output_level);

    USBManager::Config _config;
};
