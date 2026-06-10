idf_component_register(
    SRCS "firmware.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_timer gui usb_manager display
)
