#include "gui.hpp"
#include "display.hpp"
#include "esp_log.h"
#include "lgfx/v1/misc/enum.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

#define PADDING 5
#define PADDING_INNER 5
#define BORDER_WIDTH 1

static const char *TAG = "GUI";
static constexpr int AUDIO_GRAPH_SAMPLES = 233;
static uint8_t audio_history[AUDIO_GRAPH_SAMPLES] = {0};

void GUI::init(USBManager::Config config)
{
    _config = config;

    // Initialize shared data structure to zero safely on startup
    if (_config.shared_data != nullptr) {
        std::memset(_config.shared_data, 0, sizeof(mixer_data_in));
    }
}

void GUI::start()
{
    ESP_LOGI(TAG, "Launching GUI Component thread...");

    // Pass 'this' as the parameter so the static task can find our configuration data
    BaseType_t ret = xTaskCreatePinnedToCore(GUI::gui_task, "gui_task", 8192,
                                             this, // <--- Passing the instance pointer
                                             1, nullptr,
                                             1 // Core 1
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

    // 1. Calculate Root Mean Square (RMS) for energy distribution
    float sum_squares = 0.0f;
    for (int i = 0; i < channels; i++) {
        float vol = static_cast<float>(volumes[i]);
        sum_squares += (vol * vol);
    }
    float rms_volume = std::sqrt(sum_squares / channels);

    // 2. Apply ballistic physics response filter
    float alpha;
    if (rms_volume > smoothed_volume) {
        alpha = 0.40f; // Fast attack: responds immediately to audio peaks
    } else {
        alpha = 0.08f; // Slow release: smooth analog-like decay, prevents flickering
    }

    // Interpolate towards the target volume
    smoothed_volume = smoothed_volume + alpha * (rms_volume - smoothed_volume);

    // 3. Clamp and cast to final integer between 0 and 100
    int final_volume = static_cast<int>(std::round(smoothed_volume));
    return std::max(0, std::min(100, final_volume));
}

void GUI::gui_task(void *pvParameters)
{
    ESP_LOGI(TAG, "GUI Task running on Core 1.");

    // Recover the C++ class instance context
    GUI *instance = static_cast<GUI *>(pvParameters);

    auto &tft = Display::get_device();
    lgfx::LGFX_Sprite canvas(&tft);

    int w = tft.width();
    int h = tft.height();

    canvas.setFont(&fonts::efontCN_12);
    canvas.setColorDepth(lgfx::rgb565_2Byte);
    canvas.createSprite(w, h);

    bool is_woke_up = false;

    float smoothed_volume = 0.0f;

    while (true) {
        mixer_data_in local_data = {0};

        // Extract pointers out from our class-level configuration profile safely
        SemaphoreHandle_t mtx = instance->_config.mutex;
        mixer_data_in *shared = instance->_config.shared_data;

        // Take snapshot copy
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
            // audio_history[AUDIO_GRAPH_SAMPLES - 1] = *std::max_element(
            //     local_data.volumes, local_data.volumes + MIXER_CHANNELS);
            audio_history[AUDIO_GRAPH_SAMPLES - 1] = get_smoothed_master_volume(
                local_data.volumes, MIXER_CHANNELS, smoothed_volume);

            canvas.fillScreen(TFT_BLACK);

            // Call the member drawing functions using the instance pointer
            instance->draw_top_bar(canvas);
            instance->draw_vu_meters(canvas, local_data.volumes);
            instance->draw_system_stats(canvas);
            instance->draw_audio_waveform(canvas);

            // Frame outer border boundaries
            canvas.drawFastHLine(PADDING, PADDING, w - PADDING * 2, TFT_SILVER);
            canvas.drawFastHLine(PADDING, h - PADDING - 1, w - PADDING * 2, TFT_SILVER);
            canvas.drawFastVLine(PADDING, PADDING, h - PADDING * 2, TFT_SILVER);
            canvas.drawFastVLine(w - PADDING - 1, PADDING, h - PADDING * 2, TFT_SILVER);
        }

        // tft.startWrite();
        canvas.pushSprite(&tft, 0, 0);
        tft.waitDMA();
        // tft.endWrite();

        vTaskDelay(pdMS_TO_TICKS(66));
    }
}

// Note: These are now normal member functions! No static prefix needed here.
void GUI::draw_top_bar(lgfx::LGFX_Sprite &canvas)
{
    int w = canvas.width();
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_SILVER, TFT_BLACK);
    canvas.setTextDatum(TL_DATUM);

    canvas.drawString("[OUT] HEADPHONE", PADDING + BORDER_WIDTH + PADDING_INNER,
                      PADDING + BORDER_WIDTH + PADDING_INNER);

    std::string time_str = "[TIME] 19.30";
    int time_str_w = canvas.textWidth(time_str.c_str());
    canvas.drawString(time_str.c_str(),
                      w - PADDING - BORDER_WIDTH - PADDING_INNER - time_str_w,
                      PADDING + BORDER_WIDTH + PADDING_INNER);

    canvas.drawFastVLine(
        w - PADDING - BORDER_WIDTH - PADDING_INNER - time_str_w - PADDING_INNER - 1,
        PADDING, canvas.fontHeight() + PADDING_INNER * 2 + 1, TFT_SILVER);
    canvas.drawFastHLine(PADDING,
                         PADDING + BORDER_WIDTH + PADDING_INNER * 2 + canvas.fontHeight(),
                         w - PADDING * 2, TFT_SILVER);
}

/*
void GUI::draw_vu_meters(lgfx::LGFX_Sprite &canvas, uint8_t *volumes)
{
    const char *channel_names[5] = {"SYSTEM", "DISCRD", "SPOTFY", "GAMING", "BROWSR"};

    int w = canvas.width();
    int h = canvas.height();

    int channel_h =
        h - PADDING * 2 - BORDER_WIDTH * 4 - canvas.fontHeight() * 3 - PADDING_INNER * 6;
    int total_available_w = w - (PADDING * 2);
    int channel_w = total_available_w / 5;

    int fixed_bar_h = int((channel_h - PADDING_INNER * 4) * 0.8);

    for (int i = 0; i < MIXER_CHANNELS; i++) {
        int left = PADDING + (channel_w * i);
        int top = PADDING + BORDER_WIDTH * 2 + canvas.fontHeight() + PADDING_INNER * 2;

        if (i > 0) {
            canvas.drawFastVLine(left, top, channel_h, TFT_SILVER);
        }

        const int GAP = 4;

        int line_offset = (i > 0) ? 1 : 0;

        int available_w = channel_w - (PADDING_INNER * 2) - line_offset;
        int stereo_w = (available_w - GAP) / 2;

        int active_bar_h = int(fixed_bar_h * volumes[i] / 100);

        canvas.drawRect(left + PADDING_INNER + line_offset, top + PADDING_INNER, stereo_w,
                        fixed_bar_h, TFT_DARKGRAY);

        // Right Channel Bar
        canvas.drawRect(left + PADDING_INNER + line_offset + stereo_w + GAP,
                        top + PADDING_INNER, stereo_w, fixed_bar_h, TFT_DARKGREY);

        // Left Channel Bar
        canvas.fillRect(left + PADDING_INNER + line_offset,
                        top + PADDING_INNER + fixed_bar_h - active_bar_h, stereo_w,
                        active_bar_h, TFT_SILVER);

        // Right Channel Bar
        canvas.fillRect(left + PADDING_INNER + line_offset + stereo_w + GAP,
                        top + PADDING_INNER + fixed_bar_h - active_bar_h, stereo_w,
                        active_bar_h, TFT_SILVER);

        top += PADDING_INNER + fixed_bar_h;

        int str_w = canvas.textWidth(channel_names[i]);
        canvas.drawString(channel_names[i], left + int((channel_w - str_w) / 2),
                          top + PADDING_INNER);

        top += canvas.fontHeight() + PADDING_INNER;

        float mix_volume = 75.0 / 100.0;
        str_w = canvas.textWidth("75%");

        canvas.drawString("75%", left + int((channel_w - str_w) / 2), top);

        top += canvas.fontHeight();

        canvas.drawRect(left + PADDING_INNER, top + 2, channel_w - PADDING_INNER * 2, 6,
                        TFT_SILVER);
        canvas.fillRect(left + PADDING_INNER, top + 2,
                        int(mix_volume * (channel_w - PADDING_INNER * 2)), 6, TFT_SILVER);

        // canvas.drawRect(left + PADDING_INNER * 2 + str_w, top + PADDING_INNER,
        //                 10, canvas.fontHeight(),
        //                 TFT_SILVER);
    }
}


void GUI::draw_vu_meters(lgfx::LGFX_Sprite &canvas, uint8_t *volumes)
{
    const char *channel_names[5] = {"SYSTEM", "DISCRD", "SPOTFY", "GAMING", "BROWSR"};

    int w = canvas.width();
    int h = canvas.height();

    int channel_h =
        h - PADDING * 2 - BORDER_WIDTH * 4 - canvas.fontHeight() * 3 - PADDING_INNER * 6;
    int total_available_w = w - (PADDING * 2);
    int channel_w = total_available_w / 5;

    int fixed_bar_h = int((channel_h - PADDING_INNER * 4) * 0.8);

    for (int i = 0; i < MIXER_CHANNELS; i++) {
        int left = PADDING + (channel_w * i);
        int top = PADDING + BORDER_WIDTH * 2 + canvas.fontHeight() + PADDING_INNER * 2;

        if (i > 0) {
            canvas.drawFastVLine(left, top, channel_h, TFT_SILVER);
        }

        const int GAP = 4;
        int line_offset = (i > 0) ? 1 : 0;

        int available_w = channel_w - (PADDING_INNER * 2) - line_offset;
        int stereo_w = (available_w - GAP) / 2;

        int active_bar_h = int(fixed_bar_h * volumes[i] / 100);

        // --- 1. CANCELLAZIONE CHIRURGICA DEI RESIDUI FANTASMA ---
        // Calcoliamo l'altezza dello spazio vuoto sopra la barra del volume corrente
        int empty_space_h = fixed_bar_h - active_bar_h;
        if (empty_space_h > 0) {
            // Puliamo con il colore di sfondo (TFT_BLACK) l'area interna non occupata dal volume
            canvas.fillRect(left + PADDING_INNER + line_offset + 1, 
                            top + PADDING_INNER + 1, 
                            stereo_w - 2, empty_space_h, TFT_BLACK);
            
            canvas.fillRect(left + PADDING_INNER + line_offset + stereo_w + GAP + 1, 
                            top + PADDING_INNER + 1, 
                            stereo_w - 2, empty_space_h, TFT_BLACK);
        }

        // --- 2. DISEGNO DEL CONTORNO (drawRect) ---
        canvas.drawRect(left + PADDING_INNER + line_offset, top + PADDING_INNER, stereo_w,
                        fixed_bar_h, TFT_DARKGRAY);

        // Right Channel Bar Outline
        canvas.drawRect(left + PADDING_INNER + line_offset + stereo_w + GAP,
                        top + PADDING_INNER, stereo_w, fixed_bar_h, TFT_DARKGRAY);

        // --- 3. DISEGNO DELLE BARRE ATTIVE (Ancorate dentro al contorno) ---
        if (active_bar_h > 0) {
            // Left Channel Fill
            canvas.fillRect(left + PADDING_INNER + line_offset + 1,
                            top + PADDING_INNER + fixed_bar_h - active_bar_h, stereo_w - 2,
                            active_bar_h, TFT_SILVER);

            // Right Channel Fill
            canvas.fillRect(left + PADDING_INNER + line_offset + stereo_w + GAP + 1,
                            top + PADDING_INNER + fixed_bar_h - active_bar_h, stereo_w - 2,
                            active_bar_h, TFT_SILVER);
        }

        // --- 4. DISEGNO TESTO E INTERFACCIA INFERIORE ---
        top += PADDING_INNER + fixed_bar_h;

        int str_w = canvas.textWidth(channel_names[i]);
        canvas.drawString(channel_names[i], left + int((channel_w - str_w) / 2),
                          top + PADDING_INNER);

        top += canvas.fontHeight() + PADDING_INNER;

        float mix_volume = 75.0f / 100.0f; // Forzata divisione in float float-literal
        str_w = canvas.textWidth("75%");

        canvas.drawString("75%", left + int((channel_w - str_w) / 2), top);

        top += canvas.fontHeight();

        int total_bar_w = channel_w - PADDING_INNER * 2;
        
        // Disegna la barra inferiore fissa al 75% correttamente campionata
        canvas.drawRect(left + PADDING_INNER, top + 2, total_bar_w, 6, TFT_SILVER);
        
        int active_fill_w = int(mix_volume * total_bar_w);
        if (active_fill_w > 0) {
            canvas.fillRect(left + PADDING_INNER + 1, top + 3, active_fill_w - 2, 4, TFT_SILVER);
        }
    }
}
*/

void GUI::draw_vu_meters(lgfx::LGFX_Sprite &canvas, uint8_t *volumes)
{
    const char *channel_names[5] = {"SYSTEM", "DISCRD", "SPOTFY", "GAMING", "BROWSR"};

    int w = canvas.width();
    int h = canvas.height();

    int channel_h =
        h - PADDING * 2 - BORDER_WIDTH * 4 - canvas.fontHeight() * 3 - PADDING_INNER * 6;
    int total_available_w = w - (PADDING * 2);
    int channel_w = total_available_w / 5;

    int fixed_bar_h = int((channel_h - PADDING_INNER * 4) * 0.8);

    for (int i = 0; i < MIXER_CHANNELS; i++) {
        int left = PADDING + (channel_w * i);
        int top = PADDING + BORDER_WIDTH * 2 + canvas.fontHeight() + PADDING_INNER * 2;

        if (i > 0) {
            canvas.drawFastVLine(left, top, channel_h, TFT_SILVER);
        }

        const int GAP = 4;
        int line_offset = (i > 0) ? 1 : 0;

        int available_w = channel_w - (PADDING_INNER * 2) - line_offset;
        int stereo_w = (available_w - GAP) / 2;

        int active_bar_h = int(fixed_bar_h * volumes[i] / 100);

        // 1. Pulisci l'interno del box con il nero per eliminare i vecchi frame
        canvas.fillRect(left + PADDING_INNER + line_offset + 1, top + PADDING_INNER + 1, 
                        stereo_w - 2, fixed_bar_h - 2, TFT_BLACK);
        canvas.fillRect(left + PADDING_INNER + line_offset + stereo_w + GAP + 1, top + PADDING_INNER + 1, 
                        stereo_w - 2, fixed_bar_h - 2, TFT_BLACK);

        // 2. Disegna i rettangoli di contorno vuoti (Stile minimale)
        canvas.drawRect(left + PADDING_INNER + line_offset, top + PADDING_INNER, stereo_w,
                        fixed_bar_h, TFT_DARKGRAY);
        canvas.drawRect(left + PADDING_INNER + line_offset + stereo_w + GAP,
                        top + PADDING_INNER, stereo_w, fixed_bar_h, TFT_DARKGRAY);

        // 3. NUOVO: DISEGNO SEGMENTATO (A TACCHE) PER ABBATTERE IL TEARING
        const int SEGMENT_H = 2; // Altezza di ogni singola tacca in pixel
        const int SEGMENT_SPACE = 1; // Spazio vuoto tra le tacche in pixel
        int step = SEGMENT_H + SEGMENT_SPACE;

        // Disegniamo i segmenti partendo dal basso del VU-meter verso l'alto
        for (int y_offset = 0; y_offset < active_bar_h; y_offset += step) {
            int draw_y = top + PADDING_INNER + fixed_bar_h - y_offset - SEGMENT_H;
            
            // Sicurezza: non disegnare fuori dal bordo superiore del VU-meter
            if (draw_y < top + PADDING_INNER + 1) break;

            // Disegna la tacca orizzontale sul canale Sinistro
            canvas.fillRect(left + PADDING_INNER + line_offset + 1, draw_y, 
                            stereo_w - 2, SEGMENT_H, TFT_SILVER);

            // Disegna la tacca orizzontale sul canale Destro
            canvas.fillRect(left + PADDING_INNER + line_offset + stereo_w + GAP + 1, draw_y, 
                            stereo_w - 2, SEGMENT_H, TFT_SILVER);
        }

        // --- DISEGNO TESTO E INTERFACCIA INFERIORE ---
        top += PADDING_INNER + fixed_bar_h;

        int str_w = canvas.textWidth(channel_names[i]);
        canvas.drawString(channel_names[i], left + int((channel_w - str_w) / 2),
                          top + PADDING_INNER);

        top += canvas.fontHeight() + PADDING_INNER;

        float mix_volume = 75.0f / 100.0f;
        str_w = canvas.textWidth("75%");

        canvas.drawString("75%", left + int((channel_w - str_w) / 2), top);

        top += canvas.fontHeight();

        int total_bar_w = channel_w - PADDING_INNER * 2;
        canvas.drawRect(left + PADDING_INNER, top + 2, total_bar_w, 6, TFT_SILVER);

        int active_fill_w = int(mix_volume * total_bar_w);
        if (active_fill_w > 0) {
            canvas.fillRect(left + PADDING_INNER + 1, top + 3, active_fill_w - 2, 4, TFT_SILVER);
        }
    }
}

void GUI::draw_system_stats(lgfx::LGFX_Sprite &canvas)
{
    int w = canvas.width();
    int h = canvas.height();

    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> distr(0, 99);

    int ram_val = distr(gen);
    int cpu_val = distr(gen);

    std::stringstream ram_ss;
    ram_ss << "[RAM] " << std::setw(2) << std::setfill('0') << ram_val << "%";
    std::string ram_str = ram_ss.str();

    std::stringstream cpu_ss;
    cpu_ss << "[CPU] " << std::setw(2) << std::setfill('0') << cpu_val << "%";
    std::string cpu_str = cpu_ss.str();

    int ram_str_w = canvas.textWidth(ram_str.c_str());

    canvas.drawString(ram_str.c_str(), PADDING + BORDER_WIDTH + PADDING_INNER,
                      h - PADDING - BORDER_WIDTH - PADDING_INNER - canvas.fontHeight());

    canvas.drawFastHLine(PADDING + BORDER_WIDTH,
                         h - PADDING - BORDER_WIDTH - PADDING_INNER * 2 -
                             canvas.fontHeight(),
                         PADDING_INNER * 2 + ram_str_w, TFT_SILVER);
    canvas.drawFastHLine(PADDING,
                         h - PADDING - BORDER_WIDTH * 2 - PADDING_INNER * 4 -
                             canvas.fontHeight() * 2,
                         w - PADDING * 2, TFT_SILVER);
    canvas.drawFastVLine(PADDING + BORDER_WIDTH + PADDING_INNER * 2 + ram_str_w,
                         h - PADDING - BORDER_WIDTH -
                             (PADDING_INNER * 4 + BORDER_WIDTH + canvas.fontHeight() * 2),
                         PADDING_INNER * 4 + BORDER_WIDTH + canvas.fontHeight() * 2,
                         TFT_SILVER);
    canvas.drawString(cpu_str.c_str(), PADDING + BORDER_WIDTH + PADDING_INNER,
                      h - PADDING - BORDER_WIDTH * 2 - PADDING_INNER * 3 -
                          canvas.fontHeight() * 2);
}

void GUI::draw_audio_waveform(lgfx::LGFX_Sprite &canvas)
{
    int w = canvas.width();
    int h = canvas.height();

    int ram_str_w = canvas.textWidth("[RAM] 00%");

    int left = PADDING + BORDER_WIDTH * 2 + PADDING_INNER * 3 + ram_str_w;
    int top = h - PADDING - BORDER_WIDTH * 2 - PADDING_INNER * 4 -
              canvas.fontHeight() * 2 + BORDER_WIDTH + PADDING_INNER;

    int full_height = canvas.fontHeight() * 2 + BORDER_WIDTH + PADDING_INNER * 2;

    for (int i = 0; i < AUDIO_GRAPH_SAMPLES - 1; i++) {
        int height = full_height * audio_history[i] / 100;
        canvas.drawFastVLine(left + i, top + int((full_height - height) / 2), height,
                             TFT_SILVER);
    }
}
