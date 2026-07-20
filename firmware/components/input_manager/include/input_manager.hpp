#pragma once

#include "driver/gpio.h"
#include "protocol.h"
#include <cstdint>

// Reads the 2x4 key matrix and the 4 rotary encoders (CLK/DT/BTN each),
// debounces/decodes them, and pushes a PROTOCOL_CMD_* over USB CDC on every
// press/turn. Pin assignments and counts live at the top of the .cpp so a
// future hardware revision only means editing that table.
class InputManager {
  public:
    // Hardware shape, public so the pin/command tables in input_manager.cpp
    // can size their arrays off it at file scope.
    static constexpr int MATRIX_ROWS = 2;
    static constexpr int MATRIX_COLS = 4;
    static constexpr int MATRIX_KEYS = MATRIX_ROWS * MATRIX_COLS;
    static constexpr int ENCODER_COUNT = 4;

    void init();
    void start();

  private:
    // Electrical quadrature transitions per physical detent. 4 is typical
    // for cheap EC11-style encoders; tune per the actual part in use.
    static constexpr int STEPS_PER_DETENT = 4;

    static constexpr int SCAN_PERIOD_MS = 1;

    // Consecutive scans a raw reading must hold before it's accepted as a
    // real press/release, to reject mechanical switch bounce.
    static constexpr uint8_t DEBOUNCE_SCANS = 3;

    // Held after configure_gpio() and before the first scan, so pull-up/
    // matrix RC transients from power-on have died down before anything is
    // trusted (see prime_state()).
    static constexpr int STARTUP_SETTLE_MS = 50;

    static void input_task(void *pvParameters);

    void configure_gpio();
    // Reads current GPIO levels straight into the debounce state (matrix,
    // encoder buttons, encoder quadrature) without ever calling
    // send_command(). Run once at startup so the first real scan compares
    // against reality instead of an assumed "everything unpressed" state --
    // otherwise a stale/bouncing reading during power-on can look like a
    // real edge and fire a spurious command before things settle.
    void prime_state();
    void scan_matrix();
    void scan_encoders();

    static void process_edge(bool raw_pressed, uint8_t &debounce_count, bool &pressed,
                             uint8_t command);
    static void send_command(uint8_t command);

    uint8_t _matrix_debounce_count[MATRIX_KEYS] = {0};
    bool _matrix_pressed[MATRIX_KEYS] = {false};

    uint8_t _encoder_quad_state[ENCODER_COUNT] = {0};
    int8_t _encoder_step_accum[ENCODER_COUNT] = {0};

    uint8_t _encoder_btn_debounce_count[ENCODER_COUNT] = {0};
    bool _encoder_btn_pressed[ENCODER_COUNT] = {false};
};
