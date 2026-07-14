#pragma once

#include <atomic>
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

    // Runtime toggle for the audio waveform graph; the rest of the layout
    // (VU meter height, macro grid position) reflows around it every frame.
    void set_graph_visible(bool visible);
    void toggle_graph();

  private:
    // Vertical layout of the screen for a single frame, recomputed from
    // canvas height + graph visibility so sections never overlap.
    struct Layout {
        int vu_top;
        int vu_bars_base;
        int vu_bar_row_y;
        int vu_text_y;

        bool show_graph;
        int graph_top;
        int graph_bottom;

        int macro_top;
        int macro_rect_h;
        int macro_row_gap;
    };

    static Layout compute_layout(int canvas_height, bool show_graph);

    // The main loop running inside the FreeRTOS task
    static void gui_task(void *pvParameters);

    // Specific layout rendering functions (Drawing into the RAM sprite)
    void draw_top_bar(lgfx::LGFX_Sprite &canvas, int unit);
    void draw_vu_meters(lgfx::LGFX_Sprite &canvas, const channel_level *channels,
                        const metadata_packet &metadata, const Layout &layout);
    void draw_system_stats(lgfx::LGFX_Sprite &canvas, int unit);
    void draw_audio_waveform(lgfx::LGFX_Sprite &canvas, const Layout &layout);
    void draw_macro_label(lgfx::LGFX_Sprite &canvas, const Layout &layout);

    USBManager::Config _config;
    std::atomic<bool> _show_graph{true};
};
