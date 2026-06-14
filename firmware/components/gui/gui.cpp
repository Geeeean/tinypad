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

void GUI::init(USBManager::Config config)
{
    _config = config;

    
    if (_config.shared_data != nullptr) {
        std::memset(_config.shared_data, 0, sizeof(mixer_data_in));
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

int get_smoothed_master_volume(const unsigned char *volumes, int channels,
                               float &smoothed_volume)
{
    if (channels <= 0 || volumes == nullptr) {
        return 0;
    }

    
    float sum_squares = 0.0f;
    for (int i = 0; i < channels; i++) {
        float vol = static_cast<float>(volumes[i]);
        sum_squares += (vol * vol);
    }
    float rms_volume = std::sqrt(sum_squares / channels);

    
    float alpha;
    if (rms_volume > smoothed_volume) {
        alpha = 0.40f; 
    } else {
        alpha = 0.08f; 
    }

    
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

    int unit = (w - BORDER_WIDTH * 6 - PADDING * 2) / MIXER_CHANNELS;

    canvas.setFont(&fonts::efontCN_12);
    canvas.setColorDepth(lgfx::rgb565_2Byte);
    canvas.setPsram(false);
    canvas.createSprite(w, h);

    bool is_woke_up = true;

    float smoothed_volume = 0.0f;

    int i = 0;

    while (true) {
        mixer_data_in local_data = {0};

        
        SemaphoreHandle_t mtx = instance->_config.mutex;
        mixer_data_in *shared = instance->_config.shared_data;

        
        if (mtx != nullptr && shared != nullptr &&
            xSemaphoreTake(mtx, pdMS_TO_TICKS(2)) == pdTRUE) {
            local_data = *shared;

            if (local_data.valid) {
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

            std::string title = "VELVET MIXER";
            int title_str_w = canvas.textWidth(title.c_str());

            canvas.drawString("VELVET MIXER", int(w / 2 - title_str_w / 2),
                              int(h / 2 - canvas.fontHeight() / 2));
        } else {
            // Shifting array elements to the left for history graph
            for (int i = 0; i < AUDIO_GRAPH_SAMPLES - 1; i++) {
                audio_history[i] = audio_history[i + 1];
            }
            
            
            audio_history[AUDIO_GRAPH_SAMPLES - 1] = get_smoothed_master_volume(
                local_data.volumes, MIXER_CHANNELS, smoothed_volume);

            canvas.fillScreen(TFT_BLACK);
            instance->draw_audio_waveform(canvas, unit);
            instance->draw_macro_label(canvas, unit);
            instance->draw_vu_meters(canvas, local_data.volumes);
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
    canvas.drawString("VELVET", PADDING, cy);

    
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


void GUI::draw_vu_meters(lgfx::LGFX_Sprite &canvas, uint8_t *volumes)
{
    static float disp_level[MIXER_CHANNELS][2] = {{0}};

    const int w = canvas.width();
    const int count = MIXER_CHANNELS;
    const int sub_ch = 2;
    const int col_gap = 8;
    const int sub_gap = 2;

    const int bars_top = 12; 
    const int bars_base = 110;    
    const int bar_h_max = bars_base - bars_top;

    const int seg_count = 16; 
    const int seg_gap = 1;
    const int seg_h = (bar_h_max - (seg_count - 1) * seg_gap) / seg_count;

    const int vol_bar_y = 114; 
    const int vol_bar_h = 5;
    const int text_y = 122; 

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

    const char *names[MIXER_CHANNELS] = {"GAME", "CHAT", "MUSIC", "BROW", "SYS"};
    const int set_vol = 50;

    const int avail = w - 2 * PADDING - (count - 1) * col_gap;
    const int col_w = avail / count;
    const int bar_w = (col_w - sub_gap) / 2;

    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1.0f);

    for (int i = 0; i < count; i++) {
        int col_x = PADDING + i * (col_w + col_gap);

        
        for (int c = 0; c < sub_ch; c++) {
            int bx = col_x + c * (bar_w + sub_gap);

            float target = volumes[i];
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

        
        canvas.fillRect(col_x, vol_bar_y, col_w, vol_bar_h, track_color); 
        canvas.fillRect(col_x, vol_bar_y, col_w * set_vol / 100, vol_bar_h,
                        vol_fill); 

        
        canvas.setTextColor(text_col);
        canvas.setTextDatum(lgfx::textdatum_t::top_left);
        canvas.drawString(names[i], col_x, text_y);
        canvas.setTextDatum(lgfx::textdatum_t::top_right);
        canvas.drawString((std::to_string(set_vol) + "%").c_str(), col_x + col_w, text_y);
    }

    canvas.setTextDatum(lgfx::textdatum_t::top_left);
}

void GUI::draw_system_stats(lgfx::LGFX_Sprite &canvas, int unit) {}

void GUI::draw_macro_label(lgfx::LGFX_Sprite &canvas, int unit)
{
    const int w = canvas.width();
    int h = canvas.height();
    const int count = 5;
    const int gap = 6;
    const int rect_h = 14;
    const int y = h - PADDING - rect_h;

    
    const int avail = w - 2 * PADDING - (count - 1) * gap;
    const int rect_w = avail / count;

    
    canvas.setFont(&fonts::efontCN_12);
    canvas.setTextSize(1.0f);
    canvas.setTextColor(TFT_BLACK);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);

    for (int i = 0; i < count; i++) {
        int x = PADDING + i * (rect_w + gap);

        
        canvas.fillRect(x, y, rect_w, rect_h, TFT_GRAY);

        
        std::string label = "MACRO " + std::to_string(i + 1);
        canvas.drawString(label.c_str(), x + rect_w / 2, y + rect_h / 2);
    }

    canvas.setTextDatum(lgfx::textdatum_t::top_left); 
}

void GUI::draw_audio_waveform(lgfx::LGFX_Sprite &canvas, int unit)
{
    
    const int scale = 2;
    const int amp = 28;
    const int baseline = 240 - PADDING - 14 - PADDING;
    const float thickness = 1.0f;
    const int grid_color = canvas.color565(40, 40, 40);
    const int wave_color = TFT_GRAY;
    const int dark = canvas.color565(40, 40, 40);
    const int w = canvas.width();

    
    auto sample_y = [&](int idx) -> float {
        return baseline + (amp * (1.0f - audio_history[idx]) / 100.0f) * scale;
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
    const int y50 = baseline - amp * scale / 2;
    const int y100 = baseline - amp * scale;

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
