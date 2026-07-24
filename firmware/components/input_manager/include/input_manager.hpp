#pragma once

#include "driver/gpio.h"
#include "protocol.h"
#include <atomic>
#include <cstdint>

// Reads the 2x4 key matrix and 4 rotary encoders, debounces/decodes them,
// and pushes a PROTOCOL_CMD_* over USB CDC on every press/turn.
class InputManager {
  public:
    // Hardware shape, public so the pin/command tables in input_manager.cpp
    // can size their arrays off it at file scope.
    static constexpr int MATRIX_ROWS = 2;
    static constexpr int MATRIX_COLS = 4;
    static constexpr int MATRIX_KEYS = MATRIX_ROWS * MATRIX_COLS;
    static constexpr int ENCODER_COUNT = 4;

    // btn_held_out: an ENCODER_COUNT-sized array this instance mirrors its
    // debounced per-encoder button-held state into (relaxed atomics --
    // firmware.cpp owns the array, GUI reads it for the topbar's fine-step
    // indicator). May be nullptr to skip the mirroring.
    void init(std::atomic<bool> *btn_held_out);
    void start();

  private:
    // Electrical quadrature transitions per physical detent. 4 is typical
    // for cheap EC11-style encoders; tune per the actual part in use.
    static constexpr int STEPS_PER_DETENT = 4;

    static constexpr int SCAN_PERIOD_MS = 1;

    // Consecutive scans a raw reading must hold before it's accepted as a
    // real press/release, to reject mechanical switch bounce.
    static constexpr uint8_t DEBOUNCE_SCANS = 3;

    // Held after configure_gpio() so power-on GPIO transients die down
    // before anything is trusted (see prime_state()).
    static constexpr int STARTUP_SETTLE_MS = 50;

    static void input_task(void *pvParameters);

    void configure_gpio();
    // Seeds the debounce state from the current GPIO reading without ever
    // calling send_command(), so the first real scan compares against reality.
    void prime_state();
    void scan_matrix();
    void scan_encoders();

    static void process_edge(bool raw_pressed, uint8_t &debounce_count, bool &pressed,
                             uint8_t command);
    // Encoder buttons don't fit the generic press-fires-command shape above:
    // the command fires on release, and only if the encoder wasn't rotated
    // while held (a rotate-while-held gesture is a fine-step turn, not a
    // click -- see scan_encoders()).
    void process_encoder_btn_edge(int index, bool raw_pressed);
    static void send_command(uint8_t command);

    uint8_t _matrix_debounce_count[MATRIX_KEYS] = {0};
    bool _matrix_pressed[MATRIX_KEYS] = {false};

    uint8_t _encoder_quad_state[ENCODER_COUNT] = {0};
    int8_t _encoder_step_accum[ENCODER_COUNT] = {0};

    uint8_t _encoder_btn_debounce_count[ENCODER_COUNT] = {0};
    bool _encoder_btn_pressed[ENCODER_COUNT] = {false};
    bool _encoder_rotated_while_pressed[ENCODER_COUNT] = {false};

    std::atomic<bool> *_shared_btn_held = nullptr;
};
