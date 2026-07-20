#include "display.hpp"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "display_component";

#define PIN_NUM_SCLK 12
#define PIN_NUM_MOSI 11
#define PIN_NUM_MISO -1

#define PIN_NUM_LCD_CS 4
#define PIN_NUM_LCD_RST 5
#define PIN_NUM_LCD_DC 6

// Gate of the low-side N-FET switching the backlight. Boots as a floating
// input on the ESP32-S3, so it must be driven explicitly or the backlight
// is off/flickery.
#define PIN_NUM_BACKLIGHT 13

#define PANEL_NATIVE_WIDTH 240
#define PANEL_NATIVE_HEIGHT 320

static lgfx::LGFX_Device tft;

void Display::init()
{
    ESP_LOGI(
        TAG,
        "Initializing LovyanGFX ST7789 Display Driver using standard LGFX_Device...");

    // Keep the backlight off until the panel has a clean black frame on
    // screen, so power-on garbage/uninitialized RAM never flashes visibly.
    gpio_config_t bl_cfg = {};
    bl_cfg.pin_bit_mask = 1ULL << PIN_NUM_BACKLIGHT;
    bl_cfg.mode = GPIO_MODE_OUTPUT;
    bl_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    bl_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    bl_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&bl_cfg);
    gpio_set_level((gpio_num_t)PIN_NUM_BACKLIGHT, 0);

    auto bus = new lgfx::Bus_SPI();
    auto bus_cfg = bus->config();

    bus_cfg.spi_host = SPI2_HOST;  // Using SPI2_HOST (FSPI)
    bus_cfg.spi_mode = 0;          // Data latch on rising edge, clock idle low
    bus_cfg.freq_write = 40000000; // 40MHz SPI clock
    bus_cfg.freq_read = 16000000;  // 16MHz read clock
    bus_cfg.dma_channel = SPI_DMA_CH_AUTO;
    bus_cfg.pin_sclk = PIN_NUM_SCLK;
    bus_cfg.pin_mosi = PIN_NUM_MOSI;
    bus_cfg.pin_miso = PIN_NUM_MISO; // Write-only bus
    bus_cfg.pin_dc = PIN_NUM_LCD_DC;

    bus->config(bus_cfg);

    auto panel = new lgfx::Panel_ST7789();
    auto panel_cfg = panel->config();

    panel_cfg.pin_cs = PIN_NUM_LCD_CS;
    panel_cfg.pin_rst = PIN_NUM_LCD_RST;
    panel_cfg.panel_width = PANEL_NATIVE_WIDTH;   // Set native width (240)
    panel_cfg.panel_height = PANEL_NATIVE_HEIGHT; // Set native height (320)
    panel_cfg.offset_x = 0;
    panel_cfg.offset_y = 0;
    panel_cfg.invert = true;
    panel_cfg.rgb_order = false; // Standard RGB

    panel->config(panel_cfg);
    panel->setBus(bus);
    tft.setPanel(panel);
    tft.init();
    tft.setRotation(3); // landscape: panel is natively portrait (240x320)
    tft.fillScreen(TFT_BLACK);

    gpio_set_level((gpio_num_t)PIN_NUM_BACKLIGHT, 1); // screen is clean now, enable backlight

    ESP_LOGI(TAG, "ST7789 Display successfully initialized.");
}

lgfx::LGFX_Device &Display::get_device()
{
    return tft;
}
