idf_component_register(
    SRCS "gui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver protocol display usb_manager
)
