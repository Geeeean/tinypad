#include "gpio.h"
#include "driver/gpio.h"
#include "utils.h"

#define ENCODER1_SW_GPIO 1
#define ENCODER1_DT_GPIO 2
#define ENCODER1_CLK_GPIO 3

typedef struct {
  int clk_gpio;
  int dt_gpio;
} encoder_args;

static void IRAM_ATTR encoder_isr_handler(void *arg) {
  encoder_args *encoder_data = (encoder_args *)arg;

  int clk_val = gpio_get_level(encoder_data->clk_gpio);
  int dt_val = gpio_get_level(encoder_data->dt_gpio);

  // todo: send data
  if (clk_val == dt_val) {
  } else {
  }
}

void setup_gpio() {
  // config for Buttons and Encoder
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << ENCODER1_SW_GPIO) |
                                           (1ULL << ENCODER1_CLK_GPIO) |
                                           (1ULL << ENCODER1_DT_GPIO),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  // todo handle errors
  ESP_ERROR_CHECK(gpio_config(&io_conf));
  ESP_ERROR_CHECK(gpio_install_isr_service(0));

  // Setup Interrupt for CLK pin (rotation)
  gpio_set_intr_type(ENCODER1_CLK_GPIO, GPIO_INTR_ANYEDGE);
  encoder_args encoder_1 = {.clk_gpio = ENCODER1_CLK_GPIO,
                            .dt_gpio = ENCODER1_DT_GPIO};
  gpio_isr_handler_add(ENCODER1_CLK_GPIO, encoder_isr_handler,
                       (void *)&encoder_1);
}
