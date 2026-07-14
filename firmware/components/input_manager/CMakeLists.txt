idf_component_register(
    SRCS "input_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_tinyusb driver protocol
)
