idf_component_register(
    SRCS "usb_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_tinyusb driver protocol
)

# espressif__esp_tinyusb 
