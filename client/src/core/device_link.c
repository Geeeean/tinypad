#include "core/device_link.h"
#include "platform/keyboard_inject.h"
#include "platform/thread.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Encoder turns adjust volume by this many percentage points per detent.
#define ENCODER_VOLUME_STEP 5

// Finer step used when the encoder's button is held down while it's turned
// (PROTOCOL_CMD_ENCODER_*_PLUS_FINE/MINUS_FINE).
#define ENCODER_VOLUME_STEP_FINE 1

// Delay between steps of a keystroke sequence, so the target app reliably
// registers each one as a distinct keypress instead of coalescing/dropping
// events sent back-to-back.
#define MACRO_KEYSTROKE_STEP_DELAY_MS 30

// Commands decoded within this long of device_link_create() are logged and
// dropped rather than applied. Covers two real failure modes seen at
// connect time: bytes already queued in the OS serial buffer from before
// we started reading, and firmware-side electrical transients (pull-ups/
// matrix RC settling) right after power-on that can look like real button
// edges. See also input_manager's own priming on the firmware side.
#define STARTUP_GRACE_MS 500

struct device_link {
    serial_port_t *port;
    mixer_state_t *mixer;
    macro_map_t *macros;
    device_settings_t *settings;

    protocol_reader_t reader;
    metadata_packet last_sent_metadata;
    bool has_sent_metadata;
    device_config_packet last_sent_device_config;
    bool has_sent_device_config;
    uint64_t ignore_commands_until;
};

device_link_t *device_link_create(serial_port_t *port, mixer_state_t *mixer, macro_map_t *macros,
                                  device_settings_t *settings)
{
    device_link_t *link = calloc(1, sizeof(device_link_t));
    if (!link) {
        return NULL;
    }

    link->port = port;
    link->mixer = mixer;
    link->macros = macros;
    link->settings = settings;
    protocol_reader_init(&link->reader);
    link->ignore_commands_until = monotonic_ms() + STARTUP_GRACE_MS;
    return link;
}

void device_link_destroy(device_link_t *link)
{
    if (!link) {
        return;
    }
    if (link->port) {
        serial_port_close(link->port);
    }
    free(link);
}

static const char *command_name(uint8_t command)
{
    switch (command) {
    case PROTOCOL_CMD_SWITCH_1: return "SWITCH_1";
    case PROTOCOL_CMD_SWITCH_2: return "SWITCH_2";
    case PROTOCOL_CMD_SWITCH_3: return "SWITCH_3";
    case PROTOCOL_CMD_SWITCH_4: return "SWITCH_4";
    case PROTOCOL_CMD_SWITCH_5: return "SWITCH_5";
    case PROTOCOL_CMD_SWITCH_6: return "SWITCH_6";
    case PROTOCOL_CMD_SWITCH_7: return "SWITCH_7";
    case PROTOCOL_CMD_SWITCH_8: return "SWITCH_8";
    case PROTOCOL_CMD_ENCODER_1_PLUS: return "ENCODER_1_PLUS";
    case PROTOCOL_CMD_ENCODER_1_MINUS: return "ENCODER_1_MINUS";
    case PROTOCOL_CMD_ENCODER_1_BTN: return "ENCODER_1_BTN";
    case PROTOCOL_CMD_ENCODER_2_PLUS: return "ENCODER_2_PLUS";
    case PROTOCOL_CMD_ENCODER_2_MINUS: return "ENCODER_2_MINUS";
    case PROTOCOL_CMD_ENCODER_2_BTN: return "ENCODER_2_BTN";
    case PROTOCOL_CMD_ENCODER_3_PLUS: return "ENCODER_3_PLUS";
    case PROTOCOL_CMD_ENCODER_3_MINUS: return "ENCODER_3_MINUS";
    case PROTOCOL_CMD_ENCODER_3_BTN: return "ENCODER_3_BTN";
    case PROTOCOL_CMD_ENCODER_4_PLUS: return "ENCODER_4_PLUS";
    case PROTOCOL_CMD_ENCODER_4_MINUS: return "ENCODER_4_MINUS";
    case PROTOCOL_CMD_ENCODER_4_BTN: return "ENCODER_4_BTN";
    case PROTOCOL_CMD_ENCODER_1_PLUS_FINE: return "ENCODER_1_PLUS_FINE";
    case PROTOCOL_CMD_ENCODER_1_MINUS_FINE: return "ENCODER_1_MINUS_FINE";
    case PROTOCOL_CMD_ENCODER_2_PLUS_FINE: return "ENCODER_2_PLUS_FINE";
    case PROTOCOL_CMD_ENCODER_2_MINUS_FINE: return "ENCODER_2_MINUS_FINE";
    case PROTOCOL_CMD_ENCODER_3_PLUS_FINE: return "ENCODER_3_PLUS_FINE";
    case PROTOCOL_CMD_ENCODER_3_MINUS_FINE: return "ENCODER_3_MINUS_FINE";
    case PROTOCOL_CMD_ENCODER_4_PLUS_FINE: return "ENCODER_4_PLUS_FINE";
    case PROTOCOL_CMD_ENCODER_4_MINUS_FINE: return "ENCODER_4_MINUS_FINE";
    default: return "UNKNOWN";
    }
}

// Encoder plus/minus map directly to a volume nudge on the same-numbered
// slot (0-indexed), bypassing the macro map entirely.
static bool encoder_delta_for_command(uint8_t command, int *out_slot, int *out_delta)
{
    switch (command) {
    case PROTOCOL_CMD_ENCODER_1_PLUS: *out_slot = 0; *out_delta = ENCODER_VOLUME_STEP; return true;
    case PROTOCOL_CMD_ENCODER_1_MINUS: *out_slot = 0; *out_delta = -ENCODER_VOLUME_STEP; return true;
    case PROTOCOL_CMD_ENCODER_2_PLUS: *out_slot = 1; *out_delta = ENCODER_VOLUME_STEP; return true;
    case PROTOCOL_CMD_ENCODER_2_MINUS: *out_slot = 1; *out_delta = -ENCODER_VOLUME_STEP; return true;
    case PROTOCOL_CMD_ENCODER_3_PLUS: *out_slot = 2; *out_delta = ENCODER_VOLUME_STEP; return true;
    case PROTOCOL_CMD_ENCODER_3_MINUS: *out_slot = 2; *out_delta = -ENCODER_VOLUME_STEP; return true;
    case PROTOCOL_CMD_ENCODER_4_PLUS: *out_slot = 3; *out_delta = ENCODER_VOLUME_STEP; return true;
    case PROTOCOL_CMD_ENCODER_4_MINUS: *out_slot = 3; *out_delta = -ENCODER_VOLUME_STEP; return true;
    case PROTOCOL_CMD_ENCODER_1_PLUS_FINE: *out_slot = 0; *out_delta = ENCODER_VOLUME_STEP_FINE; return true;
    case PROTOCOL_CMD_ENCODER_1_MINUS_FINE: *out_slot = 0; *out_delta = -ENCODER_VOLUME_STEP_FINE; return true;
    case PROTOCOL_CMD_ENCODER_2_PLUS_FINE: *out_slot = 1; *out_delta = ENCODER_VOLUME_STEP_FINE; return true;
    case PROTOCOL_CMD_ENCODER_2_MINUS_FINE: *out_slot = 1; *out_delta = -ENCODER_VOLUME_STEP_FINE; return true;
    case PROTOCOL_CMD_ENCODER_3_PLUS_FINE: *out_slot = 2; *out_delta = ENCODER_VOLUME_STEP_FINE; return true;
    case PROTOCOL_CMD_ENCODER_3_MINUS_FINE: *out_slot = 2; *out_delta = -ENCODER_VOLUME_STEP_FINE; return true;
    case PROTOCOL_CMD_ENCODER_4_PLUS_FINE: *out_slot = 3; *out_delta = ENCODER_VOLUME_STEP_FINE; return true;
    case PROTOCOL_CMD_ENCODER_4_MINUS_FINE: *out_slot = 3; *out_delta = -ENCODER_VOLUME_STEP_FINE; return true;
    default: return false;
    }
}

static void apply_command(device_link_t *link, uint8_t command)
{
    fprintf(stderr, "device: command received: %s (0x%02X)\n", command_name(command), command);

    int slot, delta;
    if (encoder_delta_for_command(command, &slot, &delta)) {
        bool ok = mixer_state_adjust_slot_volume(link->mixer, slot, delta);
        fprintf(stderr, "  -> adjust slot %d volume by %+d%% (%s)\n", slot, delta,
                ok ? "ok" : "no session assigned to that slot");
        return;
    }

    macro_trigger_t trigger;
    if (!macro_trigger_from_command(command, &trigger)) {
        fprintf(stderr, "  -> not a recognized trigger, ignored\n");
        return;
    }

    macro_action_t action = macro_map_get(link->macros, trigger);
    switch (action.type) {
    case MACRO_ACTION_TOGGLE_MUTE_SLOT: {
        bool ok = mixer_state_toggle_slot_mute(link->mixer, action.target_slot);
        fprintf(stderr, "  -> macro: toggle mute on slot %d (%s)\n", action.target_slot,
                ok ? "ok" : "no session assigned to that slot");
        break;
    }
    case MACRO_ACTION_LOG:
        fprintf(stderr, "  -> macro: LOG action fired (test binding, no side effect)\n");
        break;
    case MACRO_ACTION_SEND_KEYSTROKE: {
        if (action.step_count == 0) {
            fprintf(stderr, "  -> macro: send-keystroke action has no steps configured\n");
            break;
        }
        for (int s = 0; s < action.step_count; s++) {
            bool ok = keyboard_inject_shortcut(action.steps[s].modifiers, action.steps[s].key);
            fprintf(stderr, "  -> macro: step %d/%d send keystroke (modifiers=0x%X key=%d) (%s)\n",
                    s + 1, action.step_count, action.steps[s].modifiers, (int)action.steps[s].key,
                    ok ? "ok" : "failed, see above");
            if (s + 1 < action.step_count) {
                sleep_ms(MACRO_KEYSTROKE_STEP_DELAY_MS);
            }
        }
        break;
    }
    case MACRO_ACTION_NONE:
    default:
        fprintf(stderr, "  -> no macro bound to this trigger\n");
        break;
    }
}

// Reads and decodes everything currently available. Returns false on a
// fatal I/O error (device unplugged etc.); true otherwise, including when
// there was simply nothing to read.
static bool pump_incoming(device_link_t *link)
{
    uint8_t chunk[64];
    int n;

    while ((n = serial_port_read(link->port, chunk, sizeof(chunk))) > 0) {
        for (int i = 0; i < n; i++) {
            uint8_t type;
            size_t size;
            protocol_feed_result_t result =
                protocol_reader_feed(&link->reader, chunk[i], &type, &size);

            if (result != PROTOCOL_FEED_COMPLETE || type != PROTOCOL_PACKET_COMMAND_EVENT) {
                continue;
            }

            command_event_packet packet;
            if (!PROTOCOL_PARSE(type, link->reader.buffer, size, &packet)) {
                continue;
            }

            if (monotonic_ms() < link->ignore_commands_until) {
                fprintf(stderr,
                        "device: ignoring %s during startup settle window (device may still be "
                        "settling, or these were buffered before we started reading)\n",
                        command_name(packet.command));
                continue;
            }

            apply_command(link, packet.command);
        }
    }

    return n != -1;
}

// Writes `packet` only if it differs from the last snapshot sent (tracked
// in *last_sent/*has_sent, which the caller owns) -- metadata and
// device_config both change rarely, so this avoids retransmitting an
// unchanged packet every tick the way levels intentionally does.
static bool send_if_changed(serial_port_t *port, const void *packet, void *last_sent,
                            bool *has_sent, size_t size)
{
    if (*has_sent && memcmp(packet, last_sent, size) == 0) {
        return true;
    }
    if (!serial_port_write(port, (const uint8_t *)packet, size)) {
        return false;
    }
    memcpy(last_sent, packet, size);
    *has_sent = true;
    return true;
}

bool device_link_poll(device_link_t *link)
{
    if (!pump_incoming(link)) {
        return false;
    }

    metadata_packet metadata;
    mixer_state_build_metadata_packet(link->mixer, &metadata);
    if (!send_if_changed(link->port, &metadata, &link->last_sent_metadata,
                         &link->has_sent_metadata, sizeof(metadata))) {
        return false;
    }

    device_config_packet device_config;
    device_settings_build_packet(link->settings, &device_config);
    if (!send_if_changed(link->port, &device_config, &link->last_sent_device_config,
                         &link->has_sent_device_config, sizeof(device_config))) {
        return false;
    }

    levels_packet levels;
    mixer_state_build_levels_packet(link->mixer, &levels);
    return serial_port_write(link->port, (const uint8_t *)&levels, sizeof(levels));
}
