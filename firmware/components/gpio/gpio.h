#pragma once

#include "freertos/idf_additions.h"

typedef struct {
} GpioParams;

void setup_gpio(void);
TaskHandle_t gpio_task_create(GpioParams *gpio_params);
