#include "gui.hpp"
#include "display.hpp"
#include "esp_log.h"
#include "lgfx/v1/misc/colortype.hpp"
#include "lgfx/v1/misc/enum.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

#define PADDING 12
#define PADDING_INNER 5
#define BORDER_WIDTH 1

static const char *TAG = "GUI";
static constexpr int AUDIO_GRAPH_SAMPLES = 250;
static uint8_t audio_history[AUDIO_GRAPH_SAMPLES] = {0};

// Shared column layout so any per-channel row (VU meters, macros, ...)
// lines up under the same MIXER_CHANNELS columns.
static constexpr int CHANNEL_COLUMN_GAP = 8;

static int channel_column_width(int canvas_width)
{
    int avail = canvas_width - 2 * PADDING - (MIXER_CHANNELS - 1) * CHANNEL_COLUMN_GAP;
    return avail / MIXER_CHANNELS;
}

static int channel_column_x(int channel_index, int column_width)
{
    return PADDING + channel_index * (column_width + CHANNEL_COLUMN_GAP);
}

// Lays out the screen top-to-bottom: VU meters, then (if enabled) the
// waveform graph, then the macro grid pinned to the bottom. Each section's
// size is derived here so none of the draw_* functions hardcode positions
// that could drift out of sync with each other.
GUI::Layout GUI::compute_layout(int canvas_height, bool show_graph)
{
    constexpr int SECTION_GAP = 8;
    constexpr int MACRO_ROWS = 2;
    constexpr int MACRO_RECT_H = 14;
    constexpr int MACRO_ROW_GAP = 4;
    constexpr int VU_TEXT_H = 12;
    constexpr int VU_TEXT_GAP = 3;
    constexpr int VU_VOL_BAR_H = 5;
    constexpr int VU_VOL_BAR_GAP = 4;
    constexpr int GRAPH_H = 56;

    Layout layout{};
    layout.show_graph = show_graph;

    const int macro_block_h = MACRO_ROWS * MACRO_RECT_H + (MACRO_ROWS - 1) * MACRO_ROW_GAP;
    const int macro_bottom = canvas_height - PADDING;
    layout.macro_top = macro_bottom - macro_block_h;
    layout.macro_rect_h = MACRO_RECT_H;
    layout.macro_row_gap = MACRO_ROW_GAP;

    const int content_top = PADDING;
    const int content_bottom = layout.macro_top - SECTION_GAP;

    int vu_bottom;
    if (show_graph) {
        layout.graph_bottom = content_bottom;
        layout.graph_top = layout.graph_bottom - GRAPH_H;
        vu_bottom = layout.graph_top - SECTION_GAP;
    } else {
        layout.graph_top = 0;
        layout.graph_bottom = 0;
        vu_bottom = content_bottom;
    }

    layout.vu_top = content_top;
    layout.vu_text_y = vu_bottom - VU_TEXT_H;
    layout.vu_bar_row_y = layout.vu_text_y - VU_TEXT_GAP - VU_VOL_BAR_H;
    layout.vu_bars_base = layout.vu_bar_row_y - VU_VOL_BAR_GAP;

    return layout;
}

void GUI::init(USBManager::Config config)
{
    _config = config;

    if (_config.shared_levels != nullptr) {
        std::memset(_config.shared_levels, 0, sizeof(levels_packet));
    }
    if (_config.shared_metadata != nullptr) {
        std::memset(_config.shared_metadata, 0, sizeof(metadata_packet));
    }
    if (_config.shared_device_config != nullptr) {
        std::memset(_config.shared_device_config, 0, sizeof(device_config_packet));
        // Graph is on by default until the host sends its own preference.
        _config.shared_device_config->show_graph = 1;
    }
}

void GUI::start()
{
    ESP_LOGI(TAG, "Launching GUI Component thread...");

    BaseType_t ret = xTaskCreatePinnedToCore(GUI::gui_task, "gui_task", 8192,
                                             this,
                                             1, nullptr,
                                             1 
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GUI Task!");
    }
}

int get_smoothed_output_level(const unsigned char *levels, int channels,
                              float &smoothed_volume)
{
    if (channels <= 0 || levels == nullptr) {
        return 0;
    }


    float sum_squares = 0.0f;
    for (int i = 0; i < channels; i++) {
        float vol = static_cast<float>(levels[i]);
        sum_squares += (vol * vol);
    }
    float rms_volume = std::sqrt(sum_squares / channels);

    // Near-raw passthrough in both directions -- deliberately jumpy rather
    // than a held/decaying VU-meter look.
    constexpr float alpha = 0.9f;
    smoothed_volume = smoothed_volume + alpha * (rms_volume - smoothed_volume);

    
    int final_volume = static_cast<int>(std::round(smoothed_volume));
    return std::max(0, std::min(100, final_volume));
}

void GUI::gui_task(void *pvParameters)
{
    ESP_LOGI(TAG, "GUI Task running on Core 1.");

    
    GUI *instance = static_cast<GUI *>(pvParameters);

    auto &tft = Display::get_device();
    lgfx::LGFX_Sprite canvas(&tft);

    int w = tft.width();
    int h = tft.height();

    canvas.setFont(&fonts::efontCN_12);
    canvas.setColorDepth(lgfx::rgb565_2Byte);
    canvas.setPsram(false);
    canvas.createSprite(w, h);

    bool is_woke_up = true;

    float smoothed_volume = 0.0f;

    int i = 0;

    while (true) {
        levels_packet local_levels = {0};
        metadata_packet local_metadata = {0};
        device_config_packet local_device_config = {0};
        local_device_config.show_graph = 1; // matches GUI::init()'s default

        SemaphoreHandle_t mtx = instance->_config.mutex;

        if (mtx != nullptr && xSemaphoreTake(mtx, pdMS_TO_TICKS(2)) == pdTRUE) {
            if (instance->_config.shared_levels != nullptr) {
                local_levels = *instance->_config.shared_levels;
            }
            if (instance->_config.shared_metadata != nullptr) {
                local_metadata = *instance->_config.shared_metadata;
            }
            if (instance->_config.shared_device_config != nullptr) {
                local_device_config = *instance->_config.shared_device_config;
            }

            if (local_levels.valid) {
                is_woke_up = true;
            }

            xSemaphoreGive(mtx);
        }

        if (!is_woke_up) {
            canvas.fillScreen(TFT_BLACK);
            int w = canvas.width();
            int h = canvas.height();

            canvas.setTextSize(2);
            canvas.setTextColor(TFT_SILVER, TFT_BLACK);
            canvas.setTextDatum(TL_DATUM);

            std::string title = "TINYPAD MIXER";
            int title_str_w = canvas.textWidth(title.c_str());

            canvas.drawString("TINYPAD MIXER", int(w / 2 - title_str_w / 2),
                              int(h / 2 - canvas.fontHeight() / 2));
        } else {
            // Shifting array elements to the left for history graph
            for (int i = 0; i < AUDIO_GRAPH_SAMPLES - 1; i++) {
                audio_history[i] = audio_history[i + 1];
            }
            // The waveform graph is labeled OUTPUT, so it tracks the actual
            // audible signal (left/right peaks, which already reflect the
            // per-channel volume gain) rather than the volume setting
            // itself -- volume alone is static between encoder turns and
            // would otherwise draw a flat line.
            uint8_t output_levels[MIXER_CHANNELS];
            for (int c = 0; c < MIXER_CHANNELS; c++) {
                output_levels[c] =
                    (local_levels.channels[c].left + local_levels.channels[c].right) / 2;
            }

            audio_history[AUDIO_GRAPH_SAMPLES - 1] =
                get_smoothed_output_level(output_levels, MIXER_CHANNELS, smoothed_volume);

            Layout layout = compute_layout(h, local_device_config.show_graph != 0);

            canvas.fillScreen(TFT_BLACK);
            instance->draw_audio_waveform(canvas, layout);
            instance->draw_macro_label(canvas, layout, local_device_config);
            instance->draw_vu_meters(canvas, local_levels.channels, local_metadata, layout);
        }

        i = (i + 1) % 3;

        tft.startWrite();
        canvas.pushSprite(&tft, 0, 0);
        tft.waitDMA();
        tft.endWrite();

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

void GUI::draw_top_bar(lgfx::LGFX_Sprite &canvas, int unit)
{
    const int w  = canvas.width();
    const int cy = 15;                 
    const int box_h     = 14;
    const int box_gap   = 6;
    const int inner_pad = 5;
    const int circle_r   = 3;
    const int circle_gap = 4;

    const int text_col   = TFT_GRAY;
    const int box_border = canvas.color565(40, 40, 40);
    const int live_green = canvas.color565(0, 200, 80);

    
    canvas.setFont(&fonts::efontCN_12_b);
    canvas.setTextSize(1.0f);
    canvas.setTextColor(TFT_SILVER);
    canvas.setTextDatum(lgfx::textdatum_t::middle_left);
    canvas.drawString("TINYPAD", PADDING, cy);

    
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextColor(text_col);
    canvas.setTextDatum(lgfx::textdatum_t::middle_right);
    std::string clock_str = "12:34";   
    canvas.drawString(clock_str.c_str(), w - PADDING, cy);

    
    int cpu = 0, ram = 0;              
    std::string cpu_str  = "CPU " + std::to_string(cpu) + "%";
    std::string ram_str  = "RAM " + std::to_string(ram) + "%";
    std::string live_str = "LIVE";

    
    int cpu_w  = canvas.textWidth(cpu_str.c_str())  + 2 * inner_pad;
    int ram_w  = canvas.textWidth(ram_str.c_str())  + 2 * inner_pad;
    int live_w = circle_r * 2 + circle_gap + canvas.textWidth(live_str.c_str()) + 2 * inner_pad;

    int group_w = cpu_w + ram_w + live_w + 2 * box_gap;
    int x = (w - group_w) / 2;          

    auto draw_box = [&](int bx, int bw, const std::string &s, bool live) {
        canvas.drawRect(bx, cy - box_h / 2, bw, box_h, box_border);   
        int tx = bx + inner_pad;
        if (live) {
            int ccx = bx + inner_pad + circle_r;
            canvas.fillSmoothCircle(ccx, cy, circle_r, live_green);
            tx = ccx + circle_r + circle_gap;
        }
        canvas.setTextColor(text_col);
        canvas.setTextDatum(lgfx::textdatum_t::middle_left);
        canvas.drawString(s.c_str(), tx, cy);
    };

    draw_box(x, cpu_w,  cpu_str,  false); x += cpu_w  + box_gap;
    draw_box(x, ram_w,  ram_str,  false); x += ram_w  + box_gap;
    draw_box(x, live_w, live_str, true);

    canvas.setTextDatum(lgfx::textdatum_t::top_left);
}


void GUI::draw_vu_meters(lgfx::LGFX_Sprite &canvas, const channel_level *channels,
                         const metadata_packet &metadata, const Layout &layout)
{
    static float disp_level[MIXER_CHANNELS][2] = {{0}};

    const int w = canvas.width();
    const int count = MIXER_CHANNELS;
    const int sub_ch = 2;
    const int sub_gap = 2;

    const int bars_top = layout.vu_top;
    const int bars_base = layout.vu_bars_base;
    const int bar_h_max = bars_base - bars_top;

    const int seg_count = 16;
    const int seg_gap = 1;
    const int seg_h = (bar_h_max - (seg_count - 1) * seg_gap) / seg_count;

    const int vol_bar_y = layout.vu_bar_row_y;
    const int vol_bar_h = 5;
    const int text_y = layout.vu_text_y;

    const int track_color = canvas.color565(40, 40, 40);
    const int green = canvas.color565(0, 200, 80);
    const int orange = canvas.color565(240, 140, 0);
    const int red = canvas.color565(230, 40, 40);
    const int vol_fill = TFT_GRAY;
    const int text_col = TFT_GRAY;

    const float up_step = 22.0f;
    const float down_step = 8.0f;

    
    const float green_max = 0.50f;
    const float orange_max = 0.70f; 

    const int col_w = channel_column_width(w);
    const int bar_w = (col_w - sub_gap) / 2;

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1.0f);

    for (int i = 0; i < count; i++) {
        int col_x = channel_column_x(i, col_w);

        const uint8_t stereo_levels[sub_ch] = {channels[i].left, channels[i].right};

        for (int c = 0; c < sub_ch; c++) {
            int bx = col_x + c * (bar_w + sub_gap);

            float target = stereo_levels[c];
            if (target > 100)
                target = 100;

            float &lv = disp_level[i][c];
            if (target > lv)
                lv = std::min(target, lv + up_step);
            else
                lv = std::max(target, lv - down_step);

            int lit = (int)(lv * seg_count / 100.0f);

            for (int s = 0; s < seg_count; s++) {
                int sy = bars_base - (s + 1) * seg_h - s * seg_gap;
                int col;
                if (s < lit) {
                    float frac = (s + 1) / (float)seg_count;
                    col = (frac <= green_max)    ? green
                          : (frac <= orange_max) ? orange
                                                 : red;
                } else {
                    col = track_color;
                }
                canvas.fillRect(bx, sy, bar_w, seg_h, col);
            }
        }

        int volume = channels[i].volume;
        canvas.fillRect(col_x, vol_bar_y, col_w, vol_bar_h, track_color);
        canvas.fillRect(col_x, vol_bar_y, col_w * volume / 100, vol_bar_h, vol_fill);

        // metadata.names[i] is a fixed 16-byte field with no guaranteed
        // null terminator (a corrupt/short packet could fill it entirely),
        // so bound the read explicitly instead of trusting it as a C
        // string. Fixed stack buffer, not std::string, since this runs
        // every frame for every channel.
        char name_buf[CHANNEL_NAME_LEN + 1];
        size_t name_len = strnlen(metadata.names[i], CHANNEL_NAME_LEN);
        if (name_len > 0) {
            std::memcpy(name_buf, metadata.names[i], name_len);
            name_buf[name_len] = '\0';
        } else {
            std::strcpy(name_buf, "---");
        }

        canvas.setTextColor(text_col);
        canvas.setTextDatum(lgfx::textdatum_t::top_left);
        canvas.drawString(name_buf, col_x, text_y);
        canvas.setTextDatum(lgfx::textdatum_t::top_right);
        canvas.drawString((std::to_string(volume) + "%").c_str(), col_x + col_w, text_y);
    }

    canvas.setTextDatum(lgfx::textdatum_t::top_left);
}

void GUI::draw_system_stats(lgfx::LGFX_Sprite &canvas, int unit) {}

void GUI::draw_macro_label(lgfx::LGFX_Sprite &canvas, const Layout &layout,
                           const device_config_packet &device_config)
{
    const int w = canvas.width();

    constexpr int cols = MIXER_CHANNELS;
    constexpr int rows = 2;
    constexpr int macro_count = rows * cols;
    static_assert(macro_count == MACRO_BUTTON_COUNT,
                  "macro grid geometry no longer matches the wire protocol's label count");

    const int row_gap = layout.macro_row_gap;
    const int rect_h = layout.macro_rect_h;

    const int rect_w = channel_column_width(w);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1.0f);
    canvas.setTextColor(TFT_BLACK);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);

    for (int i = 0; i < macro_count; i++) {
        int row = i / cols;
        int col = i % cols;

        int x = channel_column_x(col, rect_w);
        int y = layout.macro_top + row * (rect_h + row_gap);

        canvas.fillRect(x, y, rect_w, rect_h, TFT_GRAY);

        // device_config.macro_labels[i] is a fixed-length field with no
        // guaranteed null terminator, same caveat as metadata.names in
        // draw_vu_meters -- bound the read explicitly.
        size_t label_len = strnlen(device_config.macro_labels[i], MACRO_LABEL_LEN);
        std::string label = label_len > 0 ? std::string(device_config.macro_labels[i], label_len)
                                          : "MACRO " + std::to_string(i + 1);
        canvas.drawString(label.c_str(), x + rect_w / 2, y + rect_h / 2);
    }

    canvas.setTextDatum(lgfx::textdatum_t::top_left);
}

void GUI::draw_audio_waveform(lgfx::LGFX_Sprite &canvas, const Layout &layout)
{
    if (!layout.show_graph) {
        return;
    }

    const int scale = 2; // horizontal pixels per sample
    const int baseline = layout.graph_bottom;
    const int plot_h = layout.graph_bottom - layout.graph_top; // vertical pixel range
    const float thickness = 1.0f;
    const int grid_color = canvas.color565(40, 40, 40);
    const int wave_color = TFT_GRAY;
    const int dark = canvas.color565(40, 40, 40);
    const int w = canvas.width();

    auto sample_y = [&](int idx) -> float {
        return baseline + plot_h * (1.0f - audio_history[idx]) / 100.0f;
    };

    
    canvas.setTextSize(1.2f);
    const int output_w = canvas.textWidth("OUTPUT");

    const int x_left = PADDING + output_w + 10;
    const int x_right = w - PADDING;
    const int grid_w = x_right - x_left;

    int samples = grid_w / scale + 1;
    if (samples > AUDIO_GRAPH_SAMPLES)
        samples = AUDIO_GRAPH_SAMPLES;

    
    const int y0 = baseline;
    const int y50 = baseline - plot_h / 2;
    const int y100 = layout.graph_top;

    canvas.drawFastHLine(PADDING, y0, w - PADDING * 2, grid_color);
    canvas.drawFastHLine(x_left, y100, grid_w, grid_color);

    const int dash = 4, gap = 4;
    for (int dx = x_left; dx < x_right; dx += dash + gap) {
        int len = (dx + dash <= x_right) ? dash : (x_right - dx);
        canvas.drawFastHLine(dx, y50, len, grid_color);
    }

    
    float prev_x = 0, prev_y = 0;
    bool have_prev = false;
    for (int n = 0; n < samples; n++) {
        int i = AUDIO_GRAPH_SAMPLES - 1 - n;
        if (i < 0)
            break;
        float x = x_right - n * scale;
        float y = sample_y(i);
        if (have_prev) {
            canvas.drawWideLine(prev_x, prev_y, x, y, thickness, wave_color);
        }
        prev_x = x;
        prev_y = y;
        have_prev = true;
    }

    
    int last_val = (int)audio_history[AUDIO_GRAPH_SAMPLES - 1];
    int peak_val = (int)audio_history[0];
    for (int k = 1; k < AUDIO_GRAPH_SAMPLES; k++) {
        int v = (int)audio_history[k];
        if (v > peak_val)
            peak_val = v;
    }

    
    int cursor_y = y100 - 3;
    auto draw_label = [&](const std::string &s, int color, float size) {
        canvas.setTextColor(color);
        canvas.setTextSize(size);
        canvas.drawString(s.c_str(), PADDING, cursor_y);
        cursor_y += canvas.fontHeight();
    };

    draw_label("OUTPUT", dark, 1.2f);
    draw_label(std::to_string(last_val), TFT_GRAY, 2.0f);
    draw_label("PEAK " + std::to_string(peak_val), dark, 1.0f);

    canvas.setTextSize(1.0f); 
}
