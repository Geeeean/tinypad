#pragma once

// Configurable behavior for the 8 switches, 4 encoder buttons, and 8
// encoder rotation directions. Encoder rotation defaults to nudging the
// assigned slot's volume (wired directly in device_link, not through here)
// unless a macro is explicitly bound to that direction's
// MACRO_TRIGGER_ENCODER_*_ROTATE_PLUS/MINUS trigger, in which case the
// macro fires instead -- see device_link.c's apply_command().

#include "platform/keyboard_inject.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MACRO_ACTION_NONE = 0,
    MACRO_ACTION_TOGGLE_MUTE_SLOT, // target_slot: 0..MIXER_CHANNELS-1
    // Always "succeeds" and just logs -- no audio session required, so it's
    // a reliable way to confirm a physical trigger reaches device_link and
    // the macro map end-to-end (handy on the macOS stub backend, which
    // never has a real session to toggle mute on).
    MACRO_ACTION_LOG,
    // Synthesizes a system-wide keystroke *sequence*, e.g. "g" "e" "a" "n"
    // Cmd+Tab -- five steps, each with its own modifiers. A single chord is
    // just a sequence of length 1. Uses steps/step_count.
    MACRO_ACTION_SEND_KEYSTROKE,
} macro_action_type_t;

// Bounded so macro_action_t stays a plain fixed-size value (easy to copy,
// no allocation) -- generous for a macro-pad binding.
#define MACRO_KEYSTROKE_MAX_STEPS 64

typedef struct {
    uint32_t modifiers; // bitmask of macro_modifier_t
    macro_key_t key;
} macro_keystroke_step_t;

typedef struct {
    macro_action_type_t type;
    int target_slot; // MACRO_ACTION_TOGGLE_MUTE_SLOT

    // MACRO_ACTION_SEND_KEYSTROKE: executed in order, steps[0..step_count).
    macro_keystroke_step_t steps[MACRO_KEYSTROKE_MAX_STEPS];
    int step_count;
} macro_action_t;

typedef enum {
    MACRO_TRIGGER_SWITCH_1 = 0,
    MACRO_TRIGGER_SWITCH_2,
    MACRO_TRIGGER_SWITCH_3,
    MACRO_TRIGGER_SWITCH_4,
    MACRO_TRIGGER_SWITCH_5,
    MACRO_TRIGGER_SWITCH_6,
    MACRO_TRIGGER_SWITCH_7,
    MACRO_TRIGGER_SWITCH_8,
    MACRO_TRIGGER_ENCODER_1_BTN,
    MACRO_TRIGGER_ENCODER_2_BTN,
    MACRO_TRIGGER_ENCODER_3_BTN,
    MACRO_TRIGGER_ENCODER_4_BTN,
    // Appended after the discrete-button triggers above -- never renumber,
    // profile_store persists these as raw ints. Default (unbound,
    // MACRO_ACTION_NONE) means "adjust volume as usual"; binding one of
    // these makes that direction fire the macro instead.
    MACRO_TRIGGER_ENCODER_1_ROTATE_PLUS,
    MACRO_TRIGGER_ENCODER_1_ROTATE_MINUS,
    MACRO_TRIGGER_ENCODER_2_ROTATE_PLUS,
    MACRO_TRIGGER_ENCODER_2_ROTATE_MINUS,
    MACRO_TRIGGER_ENCODER_3_ROTATE_PLUS,
    MACRO_TRIGGER_ENCODER_3_ROTATE_MINUS,
    MACRO_TRIGGER_ENCODER_4_ROTATE_PLUS,
    MACRO_TRIGGER_ENCODER_4_ROTATE_MINUS,
    MACRO_TRIGGER_COUNT,
} macro_trigger_t;

typedef struct macro_map macro_map_t;

macro_map_t *macro_map_create(void);
void macro_map_destroy(macro_map_t *map);

// Default binding: each encoder button toggles mute on its own slot
// (ENCODER_n_BTN -> slot n-1); switches start unbound (MACRO_ACTION_NONE).
void macro_map_set(macro_map_t *map, macro_trigger_t trigger, macro_action_t action);
macro_action_t macro_map_get(const macro_map_t *map, macro_trigger_t trigger);

// Maps a wire PROTOCOL_CMD_SWITCH_*/PROTOCOL_CMD_ENCODER_*_BTN value to a
// trigger. Returns false for values that aren't a discrete button (i.e.
// encoder plus/minus, which device_link handles separately).
bool macro_trigger_from_command(uint8_t command, macro_trigger_t *out_trigger);
