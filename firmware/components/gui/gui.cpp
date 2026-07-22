#include "gui.hpp"
#include "display.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "lgfx/v1/misc/colortype.hpp"
#include "lgfx/v1/misc/enum.hpp"
#include "protocol.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define PADDING 12

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

// Vertical gap left between two consecutive stacked components.
static constexpr int SECTION_GAP = 8;

static constexpr int MACRO_ROWS = 2;
static constexpr int MACRO_RECT_H = 14;
static constexpr int MACRO_ROW_GAP = 4;
static constexpr int MACRO_BLOCK_H = MACRO_ROWS * MACRO_RECT_H + (MACRO_ROWS - 1) * MACRO_ROW_GAP;

static constexpr int GRAPH_H = 56;

// Fixed height for a component, or -1 for the one flexible component (VU
// meters), which takes whatever vertical space the fixed-height ones leave.
static int fixed_height_for(uint8_t component_id)
{
    switch (component_id) {
    case GUI_COMPONENT_WAVEFORM:
        return GRAPH_H;
    case GUI_COMPONENT_MACRO_GRID:
        return MACRO_BLOCK_H;
    default:
        return -1;
    }
}

// Stacks the enabled components top to bottom. Two passes, since the
// flexible component's height needs every other component's height first.
GUI::Layout GUI::compute_layout(int canvas_height, const uint8_t gui_layout[GUI_COMPONENT_COUNT])
{
    Layout layout{};

    int enabled_count = 0;
    int fixed_total = 0;
    bool vu_enabled = false;

    for (int i = 0; i < GUI_COMPONENT_COUNT; i++) {
        uint8_t id = gui_layout[i];
        if (id == GUI_COMPONENT_NONE) {
            continue;
        }
        enabled_count++;
        int fixed_h = fixed_height_for(id);
        if (fixed_h < 0) {
            vu_enabled = true;
        } else {
            fixed_total += fixed_h;
        }
    }

    int gaps = enabled_count > 0 ? (enabled_count - 1) * SECTION_GAP : 0;
    int available = canvas_height - 2 * PADDING - gaps;
    int vu_height = vu_enabled ? std::max(0, available - fixed_total) : 0;

    int y = PADDING;
    for (int i = 0; i < GUI_COMPONENT_COUNT; i++) {
        uint8_t id = gui_layout[i];
        if (id == GUI_COMPONENT_NONE) {
            continue;
        }

        int fixed_h = fixed_height_for(id);
        int h = fixed_h < 0 ? vu_height : fixed_h;

        layout.bounds[id] = {y, y + h};
        y += h + SECTION_GAP;
    }

    return layout;
}

// All three pieces on, original order -- the fallback until the host sends
// its own gui_layout preference.
static void set_default_gui_layout(uint8_t gui_layout[GUI_COMPONENT_COUNT])
{
    gui_layout[0] = GUI_COMPONENT_VU_METERS;
    gui_layout[1] = GUI_COMPONENT_WAVEFORM;
    gui_layout[2] = GUI_COMPONENT_MACRO_GRID;
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
        set_default_gui_layout(_config.shared_device_config->gui_layout);
    }
}

void GUI::start()
{
    ESP_LOGI(TAG, "Launching GUI Component thread...");

    BaseType_t ret =
        xTaskCreatePinnedToCore(GUI::gui_task, "gui_task", 8192, this, 1, nullptr, 1);

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

    // Shows the idle splash screen until the host is alive (a levels packet
    // arrived recently), regardless of whether it has any audio to show yet.
    bool is_woke_up = false;
    static constexpr int64_t LEVELS_STALE_TIMEOUT_US = 750 * 1000; // 750ms

    float smoothed_volume = 0.0f;

    while (true) {
        levels_packet local_levels = {};
        metadata_packet local_metadata = {};
        device_config_packet local_device_config = {};
        set_default_gui_layout(local_device_config.gui_layout);
        int64_t local_last_levels_us = 0;

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
            if (instance->_config.shared_last_levels_us != nullptr) {
                local_last_levels_us = *instance->_config.shared_last_levels_us;
            }

            xSemaphoreGive(mtx);
        }

        // Falls back to the idle screen once levels packets stop arriving.
        // Excludes local_last_levels_us == 0 (never received), which would look "recent" at boot.
        bool has_ever_received_levels = local_last_levels_us != 0;
        int64_t elapsed_us = esp_timer_get_time() - local_last_levels_us;
        is_woke_up = has_ever_received_levels && elapsed_us < LEVELS_STALE_TIMEOUT_US;

        // A packet fresh off the wire isn't guaranteed to be normalized, so
        // normalize the local copy before compute_layout/the draw loop use it.
        protocol_normalize_gui_layout(local_device_config.gui_layout);

        if (!is_woke_up) {
            canvas.fillScreen(TFT_BLACK);
            int w = canvas.width();
            int h = canvas.height();

            canvas.setTextSize(2);
            canvas.setTextColor(TFT_SILVER, TFT_BLACK);
            canvas.setTextDatum(TL_DATUM);

            std::string title = "TINYPAD";
            int title_str_w = canvas.textWidth(title.c_str());

            canvas.drawString("TINYPAD", int(w / 2 - title_str_w / 2),
                              int(h / 2 - canvas.fontHeight() / 2));
        } else {
            std::memmove(audio_history, audio_history + 1, AUDIO_GRAPH_SAMPLES - 1);

            // Tracks the actual audible signal (left/right peaks), not the
            // volume setting, which is static between encoder turns. Only
            // channels currently producing signal count toward the average --
            // an assigned+unmuted slot that just isn't making any sound right
            // now would otherwise dilute the reading same as an empty one.
            uint8_t output_levels[MIXER_CHANNELS];
            int active_channels = 0;
            for (int c = 0; c < MIXER_CHANNELS; c++) {
                const channel_level &ch = local_levels.channels[c];
                if (ch.volume == 0 || ch.muted) {
                    continue;
                }
                uint8_t level = (ch.left + ch.right) / 2;
                if (level == 0) {
                    continue;
                }
                output_levels[active_channels++] = level;
            }

            audio_history[AUDIO_GRAPH_SAMPLES - 1] =
                get_smoothed_output_level(output_levels, active_channels, smoothed_volume);

            Layout layout = compute_layout(h, local_device_config.gui_layout);

            canvas.fillScreen(TFT_BLACK);
            for (int i = 0; i < GUI_COMPONENT_COUNT; i++) {
                uint8_t id = local_device_config.gui_layout[i];
                switch (id) {
                case GUI_COMPONENT_VU_METERS:
                    instance->draw_vu_meters(canvas, layout.bounds[id], local_levels.channels,
                                             local_metadata);
                    break;
                case GUI_COMPONENT_WAVEFORM:
                    instance->draw_audio_waveform(canvas, layout.bounds[id]);
                    break;
                case GUI_COMPONENT_MACRO_GRID:
                    instance->draw_macro_label(canvas, layout.bounds[id], local_device_config);
                    break;
                default:
                    break; // GUI_COMPONENT_NONE: this slot is disabled
                }
            }
        }

        Display::wait_for_vsync();

        tft.startWrite();
        canvas.pushSprite(&tft, 0, 0);
        tft.waitDMA();
        tft.endWrite();
    }
}

// Scales an RGB565 color's brightness by `factor` (0-1) -- against this UI's
// black background, blending with black at alpha a is just color*a.
static uint16_t dim_color565(uint16_t color, float factor)
{
    uint16_t r = (color >> 11) & 0x1F;
    uint16_t g = (color >> 5) & 0x3F;
    uint16_t b = color & 0x1F;
    r = (uint16_t)(r * factor);
    g = (uint16_t)(g * factor);
    b = (uint16_t)(b * factor);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Applied to every element of a muted channel's column so mute reads
// clearly at a glance, not as a subtle change to one piece of it.
static constexpr float MUTED_OPACITY = 0.45f;

static uint16_t dim_if_muted(uint16_t color, bool muted)
{
    return muted ? dim_color565(color, MUTED_OPACITY) : color;
}

// Linearly blends two RGB565 colors component-wise as t goes 0 (a) -> 1 (b).
static uint16_t lerp_color565(uint16_t a, uint16_t b, float t)
{
    t = std::max(0.0f, std::min(1.0f, t));
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + (int)((br - ar) * t);
    int g = ag + (int)((bg - ag) * t);
    int b2 = ab + (int)((bb - ab) * t);
    return (uint16_t)((r << 11) | (g << 5) | b2);
}

// Smooth green->orange->red gradient by signal level, instead of a hard
// cutoff at each threshold.
static uint16_t vu_gradient_color(uint16_t green, uint16_t orange, uint16_t red, float green_max,
                                  float orange_max, float frac)
{
    if (frac <= green_max) {
        return lerp_color565(green, orange, frac / green_max);
    }
    if (frac <= orange_max) {
        return lerp_color565(orange, red, (frac - green_max) / (orange_max - green_max));
    }
    return red;
}

// Shortens text with a trailing "..." until it fits max_width, so a long
// channel name can't overlap the percentage drawn to its right.
static std::string truncate_to_width(lgfx::LGFX_Sprite &canvas, const std::string &text,
                                     int max_width)
{
    if (canvas.textWidth(text.c_str()) <= max_width) {
        return text;
    }

    static constexpr const char *ELLIPSIS = "...";
    for (int len = (int)text.size() - 1; len > 0; len--) {
        std::string candidate = text.substr(0, len) + ELLIPSIS;
        if (canvas.textWidth(candidate.c_str()) <= max_width) {
            return candidate;
        }
    }
    return ELLIPSIS;
}

void GUI::draw_vu_meters(lgfx::LGFX_Sprite &canvas, const Bounds &bounds,
                         const channel_level *channels, const metadata_packet &metadata)
{
    static float disp_level[MIXER_CHANNELS][2] = {{0}};

    constexpr int VU_TEXT_H = 12;
    constexpr int VU_TEXT_GAP = 3;
    constexpr int VU_VOL_BAR_H = 5;
    constexpr int VU_VOL_BAR_GAP = 4;

    const int w = canvas.width();
    const int count = MIXER_CHANNELS;
    const int sub_ch = 2;
    const int sub_gap = 2;

    const int text_y = bounds.bottom - VU_TEXT_H;
    const int vol_bar_y = text_y - VU_TEXT_GAP - VU_VOL_BAR_H;
    const int bars_base = vol_bar_y - VU_VOL_BAR_GAP;
    const int bars_top = bounds.top;
    const int bar_h_max = bars_base - bars_top;

    const int seg_count = 16;
    const int seg_gap = 1;
    const int seg_h = (bar_h_max - (seg_count - 1) * seg_gap) / seg_count;

    const int vol_bar_h = VU_VOL_BAR_H;

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
        bool muted = channels[i].muted != 0;

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
                // Anchored from bars_base (bottom) so segment size is fixed;
                // any rounding leftover becomes a margin above the top segment.
                int sy = bars_base - seg_h - s * (seg_h + seg_gap);
                int col;
                if (s < lit) {
                    float frac = (s + 1) / (float)seg_count;
                    col = vu_gradient_color(green, orange, red, green_max, orange_max, frac);
                } else {
                    col = track_color;
                }
                col = dim_if_muted((uint16_t)col, muted);
                canvas.fillRect(bx, sy, bar_w, seg_h, col);
            }
        }

        int volume = channels[i].volume;
        int this_track = dim_if_muted((uint16_t)track_color, muted);
        int this_vol_fill = dim_if_muted((uint16_t)vol_fill, muted);
        canvas.fillRect(col_x, vol_bar_y, col_w, vol_bar_h, this_track);
        canvas.fillRect(col_x, vol_bar_y, col_w * volume / 100, vol_bar_h, this_vol_fill);

        // metadata.names[i] has no guaranteed null terminator, so bound the
        // read explicitly instead of trusting it as a C string.
        char name_buf[CHANNEL_NAME_LEN + 1];
        size_t name_len = strnlen(metadata.names[i], CHANNEL_NAME_LEN);
        if (name_len > 0) {
            std::memcpy(name_buf, metadata.names[i], name_len);
            name_buf[name_len] = '\0';
        } else {
            std::strcpy(name_buf, "---");
        }

        std::string pct_str = std::to_string(volume) + "%";
        int pct_w = canvas.textWidth(pct_str.c_str());
        constexpr int NAME_PCT_GAP = 4;
        std::string display_name =
            truncate_to_width(canvas, name_buf, col_w - pct_w - NAME_PCT_GAP);

        int this_text_col = dim_if_muted((uint16_t)text_col, muted);
        canvas.setTextColor(this_text_col);
        canvas.setTextDatum(lgfx::textdatum_t::top_left);
        canvas.drawString(display_name.c_str(), col_x, text_y);
        canvas.setTextDatum(lgfx::textdatum_t::top_right);
        canvas.drawString(pct_str.c_str(), col_x + col_w, text_y);
    }

    canvas.setTextDatum(lgfx::textdatum_t::top_left);
}

void GUI::draw_macro_label(lgfx::LGFX_Sprite &canvas, const Bounds &bounds,
                           const device_config_packet &device_config)
{
    const int w = canvas.width();

    constexpr int cols = MIXER_CHANNELS;
    constexpr int rows = MACRO_ROWS;
    constexpr int macro_count = rows * cols;
    static_assert(macro_count == MACRO_BUTTON_COUNT,
                  "macro grid geometry no longer matches the wire protocol's label count");

    const int row_gap = MACRO_ROW_GAP;
    const int rect_h = MACRO_RECT_H;

    const int rect_w = channel_column_width(w);

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1.0f);
    canvas.setTextColor(TFT_BLACK);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);

    for (int i = 0; i < macro_count; i++) {
        int row = i / cols;
        int col = i % cols;

        int x = channel_column_x(col, rect_w);
        int y = bounds.top + row * (rect_h + row_gap);

        canvas.fillRect(x, y, rect_w, rect_h, TFT_GRAY);

        // Same no-guaranteed-terminator caveat as metadata.names above.
        size_t label_len = strnlen(device_config.macro_labels[i], MACRO_LABEL_LEN);
        std::string label = label_len > 0 ? std::string(device_config.macro_labels[i], label_len)
                                          : "MACRO " + std::to_string(i + 1);
        canvas.drawString(label.c_str(), x + rect_w / 2, y + rect_h / 2);
    }

    canvas.setTextDatum(lgfx::textdatum_t::top_left);
}

void GUI::draw_audio_waveform(lgfx::LGFX_Sprite &canvas, const Bounds &bounds)
{
    const int scale = 2; // horizontal pixels per sample
    const int baseline = bounds.bottom;
    const int plot_h = bounds.bottom - bounds.top; // vertical pixel range
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
    const int y100 = bounds.top;

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
