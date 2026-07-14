#include "input_manager.hpp"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"

static const char *TAG = "INPUT";

// --- ESP32-S3-WROOM-1 pin assignments -------------------------------------
// Key matrix: 2 rows x 4 columns = 8 switches. The current PCB also wires a
// 5th column (COL_5 = GPIO13) but the next revision drops it, so it's left
// unused here.
static constexpr gpio_num_t ROW_PINS[InputManager::MATRIX_ROWS] = {GPIO_NUM_1, GPIO_NUM_2};
static constexpr gpio_num_t COL_PINS[InputManager::MATRIX_COLS] = {GPIO_NUM_7, GPIO_NUM_8,
                                                                    GPIO_NUM_9, GPIO_NUM_10};

// Rotary encoders: CLK, DT, BTN per encoder. The current PCB also wires a
// 5th encoder (CLK=GPIO18, DT=GPIO41, BTN=GPIO48) but the next revision
// drops it, so it's left unused here.
static constexpr gpio_num_t ENCODER_CLK_PINS[InputManager::ENCODER_COUNT] = {
    GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17};
static constexpr gpio_num_t ENCODER_DT_PINS[InputManager::ENCODER_COUNT] = {
    GPIO_NUM_21, GPIO_NUM_38, GPIO_NUM_39, GPIO_NUM_40};
static constexpr gpio_num_t ENCODER_BTN_PINS[InputManager::ENCODER_COUNT] = {
    GPIO_NUM_42, GPIO_NUM_45, GPIO_NUM_46, GPIO_NUM_47};

// --- Wire commands per physical input --------------------------------------
static constexpr Command SWITCH_COMMANDS[InputManager::MATRIX_KEYS] = {
    Command::SWITCH_1, Command::SWITCH_2, Command::SWITCH_3, Command::SWITCH_4,
    Command::SWITCH_5, Command::SWITCH_6, Command::SWITCH_7, Command::SWITCH_8,
};

static constexpr Command ENCODER_PLUS_COMMANDS[InputManager::ENCODER_COUNT] = {
    Command::ENCODER_1_PLUS, Command::ENCODER_2_PLUS, Command::ENCODER_3_PLUS,
    Command::ENCODER_4_PLUS,
};
static constexpr Command ENCODER_MINUS_COMMANDS[InputManager::ENCODER_COUNT] = {
    Command::ENCODER_1_MINUS, Command::ENCODER_2_MINUS, Command::ENCODER_3_MINUS,
    Command::ENCODER_4_MINUS,
};
static constexpr Command ENCODER_BTN_COMMANDS[InputManager::ENCODER_COUNT] = {
    Command::ENCODER_1_BTN, Command::ENCODER_2_BTN, Command::ENCODER_3_BTN,
    Command::ENCODER_4_BTN,
};

// Standard 2-bit Gray code quadrature table, indexed by (prev_state << 2 |
// curr_state). Yields +1/-1 on a valid single-step transition, 0 on no
// change or an invalid/skipped transition.
static constexpr int8_t QUADRATURE_TABLE[16] = {
    0, -1, 1,  0, //
    1, 0,  0,  -1, //
    -1, 0, 0,  1, //
    0, 1,  -1, 0,
};

static void configure_input_pin(gpio_num_t pin)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void InputManager::configure_gpio()
{
    for (gpio_num_t pin : ROW_PINS) {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = 1ULL << pin;
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&cfg);
        gpio_set_level(pin, 1); // idle high, rows are driven low one at a time to scan
    }

    for (gpio_num_t pin : COL_PINS) {
        configure_input_pin(pin);
    }

    for (int e = 0; e < ENCODER_COUNT; e++) {
        configure_input_pin(ENCODER_CLK_PINS[e]);
        configure_input_pin(ENCODER_DT_PINS[e]);
        configure_input_pin(ENCODER_BTN_PINS[e]);
    }
}

void InputManager::init()
{
    ESP_LOGI(TAG, "Configuring input GPIOs...");
    configure_gpio();
}

// Mirrors scan_matrix()/scan_encoders()'s readings but writes straight into
// the debounce state instead of going through process_edge()/send_command()
// -- this is a baseline snapshot, not an edge detection pass.
void InputManager::prime_state()
{
    for (int r = 0; r < MATRIX_ROWS; r++) {
        gpio_set_level(ROW_PINS[r], 0);
        esp_rom_delay_us(5);

        for (int c = 0; c < MATRIX_COLS; c++) {
            int key = r * MATRIX_COLS + c;
            _matrix_pressed[key] = (gpio_get_level(COL_PINS[c]) == 0);
            _matrix_debounce_count[key] = 0;
        }

        gpio_set_level(ROW_PINS[r], 1);
    }

    for (int e = 0; e < ENCODER_COUNT; e++) {
        uint8_t clk = gpio_get_level(ENCODER_CLK_PINS[e]);
        uint8_t dt = gpio_get_level(ENCODER_DT_PINS[e]);
        _encoder_quad_state[e] = (clk << 1) | dt;
        _encoder_step_accum[e] = 0;

        _encoder_btn_pressed[e] = (gpio_get_level(ENCODER_BTN_PINS[e]) == 0);
        _encoder_btn_debounce_count[e] = 0;
    }
}

void InputManager::start()
{
    ESP_LOGI(TAG, "Spawning input processing task...");

    BaseType_t task_ret = xTaskCreatePinnedToCore(InputManager::input_task, "input_manager_task",
                                                  4096, this, 5, nullptr, 0);

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to spin up input task!");
    }
}

void InputManager::input_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Input Thread alive.");

    InputManager *instance = static_cast<InputManager *>(pvParameters);

    // Give power-on electrical transients (pull-ups charging, matrix RC
    // settle) time to die down, then seed the debounce state from the
    // actual current reading before the first real scan runs.
    vTaskDelay(pdMS_TO_TICKS(STARTUP_SETTLE_MS));
    instance->prime_state();

    while (true) {
        instance->scan_matrix();
        instance->scan_encoders();

        vTaskDelay(pdMS_TO_TICKS(SCAN_PERIOD_MS));
    }
}

void InputManager::process_edge(bool raw_pressed, uint8_t &debounce_count, bool &pressed,
                                Command command)
{
    if (raw_pressed == pressed) {
        debounce_count = 0;
        return;
    }

    // The raw reading disagrees with the accepted state; require it to
    // stay consistent for a few scans in a row before trusting it.
    debounce_count++;
    if (debounce_count < DEBOUNCE_SCANS) {
        return;
    }

    debounce_count = 0;
    pressed = raw_pressed;

    if (pressed) {
        send_command(command);
    }
}

void InputManager::scan_matrix()
{
    // Rows idle high; pull one low at a time to scan it, then restore it
    // before moving to the next (O(rows) GPIO writes, not O(rows^2)).
    for (int r = 0; r < MATRIX_ROWS; r++) {
        gpio_set_level(ROW_PINS[r], 0);
        esp_rom_delay_us(5); // let the row line settle before sampling columns

        for (int c = 0; c < MATRIX_COLS; c++) {
            int key = r * MATRIX_COLS + c;
            bool raw_pressed = (gpio_get_level(COL_PINS[c]) == 0);
            process_edge(raw_pressed, _matrix_debounce_count[key], _matrix_pressed[key],
                        SWITCH_COMMANDS[key]);
        }

        gpio_set_level(ROW_PINS[r], 1);
    }
}

void InputManager::scan_encoders()
{
    for (int e = 0; e < ENCODER_COUNT; e++) {
        uint8_t clk = gpio_get_level(ENCODER_CLK_PINS[e]);
        uint8_t dt = gpio_get_level(ENCODER_DT_PINS[e]);
        uint8_t btn_raw = gpio_get_level(ENCODER_BTN_PINS[e]);

        uint8_t curr_state = (clk << 1) | dt;

        uint8_t index = (_encoder_quad_state[e] << 2) | curr_state;
        int8_t step = QUADRATURE_TABLE[index & 0x0F];
        _encoder_quad_state[e] = curr_state;

        if (step != 0) {
            _encoder_step_accum[e] += step;

            if (_encoder_step_accum[e] >= STEPS_PER_DETENT) {
                _encoder_step_accum[e] = 0;
                send_command(ENCODER_PLUS_COMMANDS[e]);
            } else if (_encoder_step_accum[e] <= -STEPS_PER_DETENT) {
                _encoder_step_accum[e] = 0;
                send_command(ENCODER_MINUS_COMMANDS[e]);
            }
        }

        bool raw_pressed = (btn_raw == 0);
        process_edge(raw_pressed, _encoder_btn_debounce_count[e], _encoder_btn_pressed[e],
                    ENCODER_BTN_COMMANDS[e]);
    }
}

void InputManager::send_command(Command command)
{
    command_event_packet packet;
    Protocol::build_command_event_packet(command, &packet);

    tud_cdc_n_write(0, &packet, sizeof(packet));
    tud_cdc_n_write_flush(0);
}
