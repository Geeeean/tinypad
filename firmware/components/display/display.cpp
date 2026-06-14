#include "display.hpp"
#include "esp_log.h"

static const char *TAG = "display_component";

#define PIN_NUM_SCLK 12
#define PIN_NUM_MOSI 11
#define PIN_NUM_MISO -1

#define PIN_NUM_LCD_CS 4
#define PIN_NUM_LCD_RST 5
#define PIN_NUM_LCD_DC 6

#define PANEL_NATIVE_WIDTH 320
#define PANEL_NATIVE_HEIGHT 240

static lgfx::LGFX_Device tft;

void Display::init()
{
    ESP_LOGI(
        TAG,
        "Initializing LovyanGFX ILI9341 Display Driver using standard LGFX_Device...");

    auto bus = new lgfx::Bus_SPI();
    auto bus_cfg = bus->config();

    bus_cfg.spi_host = SPI2_HOST;  // Using SPI2_HOST (FSPI)
    bus_cfg.spi_mode = 0;          // Data latch on rising edge, clock idle low
    bus_cfg.freq_write = 80000000; // 40MHz SPI clock
    bus_cfg.freq_read = 16000000;  // 16MHz read clock
    bus_cfg.dma_channel = SPI_DMA_CH_AUTO;
    bus_cfg.pin_sclk = PIN_NUM_SCLK;
    bus_cfg.pin_mosi = PIN_NUM_MOSI;
    bus_cfg.pin_miso = PIN_NUM_MISO; // Write-only bus
    bus_cfg.pin_dc = PIN_NUM_LCD_DC;

    bus->config(bus_cfg);

    auto panel = new lgfx::Panel_ILI9342();
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
    tft.fillScreen(TFT_BLACK);
    ESP_LOGI(TAG, "ILI9341 Display successfully initialized.");
}

lgfx::LGFX_Device &Display::get_device()
{
    return tft;
}
