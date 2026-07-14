#include "core/macro_map.h"
#include "protocol.h"
#include <stdlib.h>

struct macro_map {
    macro_action_t triggers[MACRO_TRIGGER_COUNT];
};

macro_map_t *macro_map_create(void)
{
    macro_map_t *map = calloc(1, sizeof(macro_map_t));
    if (!map) {
        return NULL;
    }

    for (int i = 0; i < MACRO_TRIGGER_COUNT; i++) {
        map->triggers[i] = (macro_action_t){.type = MACRO_ACTION_NONE, .target_slot = -1};
    }

    // Sensible default so the device is useful out of the box: each
    // encoder's own button mutes the channel it controls.
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        map->triggers[MACRO_TRIGGER_ENCODER_1_BTN + i] =
            (macro_action_t){.type = MACRO_ACTION_TOGGLE_MUTE_SLOT, .target_slot = i};
    }

    return map;
}

void macro_map_destroy(macro_map_t *map) { free(map); }

void macro_map_set(macro_map_t *map, macro_trigger_t trigger, macro_action_t action)
{
    if (!map || trigger < 0 || trigger >= MACRO_TRIGGER_COUNT) {
        return;
    }
    map->triggers[trigger] = action;
}

macro_action_t macro_map_get(const macro_map_t *map, macro_trigger_t trigger)
{
    if (!map || trigger < 0 || trigger >= MACRO_TRIGGER_COUNT) {
        return (macro_action_t){.type = MACRO_ACTION_NONE, .target_slot = -1};
    }
    return map->triggers[trigger];
}

bool macro_trigger_from_command(uint8_t command, macro_trigger_t *out_trigger)
{
    switch (command) {
    case PROTOCOL_CMD_SWITCH_1: *out_trigger = MACRO_TRIGGER_SWITCH_1; return true;
    case PROTOCOL_CMD_SWITCH_2: *out_trigger = MACRO_TRIGGER_SWITCH_2; return true;
    case PROTOCOL_CMD_SWITCH_3: *out_trigger = MACRO_TRIGGER_SWITCH_3; return true;
    case PROTOCOL_CMD_SWITCH_4: *out_trigger = MACRO_TRIGGER_SWITCH_4; return true;
    case PROTOCOL_CMD_SWITCH_5: *out_trigger = MACRO_TRIGGER_SWITCH_5; return true;
    case PROTOCOL_CMD_SWITCH_6: *out_trigger = MACRO_TRIGGER_SWITCH_6; return true;
    case PROTOCOL_CMD_SWITCH_7: *out_trigger = MACRO_TRIGGER_SWITCH_7; return true;
    case PROTOCOL_CMD_SWITCH_8: *out_trigger = MACRO_TRIGGER_SWITCH_8; return true;
    case PROTOCOL_CMD_ENCODER_1_BTN: *out_trigger = MACRO_TRIGGER_ENCODER_1_BTN; return true;
    case PROTOCOL_CMD_ENCODER_2_BTN: *out_trigger = MACRO_TRIGGER_ENCODER_2_BTN; return true;
    case PROTOCOL_CMD_ENCODER_3_BTN: *out_trigger = MACRO_TRIGGER_ENCODER_3_BTN; return true;
    case PROTOCOL_CMD_ENCODER_4_BTN: *out_trigger = MACRO_TRIGGER_ENCODER_4_BTN; return true;
    default: return false;
    }
}
