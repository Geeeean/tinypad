#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "protocol.hpp"

class USBManager {
  public:
    typedef struct {
        mixer_data_in *shared_data;
        SemaphoreHandle_t mutex;
    } Config;

    void init(Config config);
    void start();

  private:
    static void usb_task(void *pvParameters);
    bool receive_data(mixer_data_in *out_data);

    Config _config;
};
