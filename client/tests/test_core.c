// Minimal assert-based test runner for the platform-independent core/
// modules (protocol framing, macro_map, device_settings, mixer_state).
// No framework dependency, matching the rest of this project's style --
// each RUN_TEST prints a line and aborts on the first failure so a red run
// points straight at the offending assertion.

#include "core/device_settings.h"
#include "core/macro_map.h"
#include "core/mixer_state.h"
#include "platform/audio_backend.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

#define CHECK(cond)                                                                             \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            g_failures++;                                                                        \
        }                                                                                         \
    } while (0)

#define RUN_TEST(fn)                                                                             \
    do {                                                                                         \
        printf("-- %s\n", #fn);                                                                  \
        fn();                                                                                    \
    } while (0)

// --- protocol.h ------------------------------------------------------------

static void test_protocol_levels_roundtrip(void)
{
    channel_level channels[MIXER_CHANNELS] = {
        {.volume = 10, .left = 20, .right = 30},
        {.volume = 40, .left = 50, .right = 60},
        {.volume = 70, .left = 80, .right = 90},
        {.volume = 100, .left = 0, .right = 1},
    };
    levels_packet built;
    protocol_build_levels_packet(&built, channels, 1);

    levels_packet parsed;
    CHECK(protocol_parse_levels_packet((const uint8_t *)&built, sizeof(built), &parsed));
    CHECK(parsed.header == PROTOCOL_START_BYTE);
    CHECK(parsed.type == PROTOCOL_PACKET_LEVELS);
    CHECK(parsed.valid == 1);
    CHECK(memcmp(parsed.channels, channels, sizeof(channels)) == 0);
}

static void test_protocol_metadata_roundtrip(void)
{
    char names[MIXER_CHANNELS][CHANNEL_NAME_LEN] = {"Discord", "Spotify", "Game", "Browser"};
    metadata_packet built;
    protocol_build_metadata_packet(&built, names);

    metadata_packet parsed;
    CHECK(protocol_parse_metadata_packet((const uint8_t *)&built, sizeof(built), &parsed));
    CHECK(strcmp(parsed.names[0], "Discord") == 0);
    CHECK(strcmp(parsed.names[3], "Browser") == 0);
}

static void test_protocol_device_config_roundtrip(void)
{
    char labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN] = {0};
    strcpy(labels[0], "MUTE");
    strcpy(labels[7], "PLAY");

    device_config_packet built;
    protocol_build_device_config_packet(&built, labels, 1);

    device_config_packet parsed;
    CHECK(protocol_parse_device_config_packet((const uint8_t *)&built, sizeof(built), &parsed));
    CHECK(strcmp(parsed.macro_labels[0], "MUTE") == 0);
    CHECK(strcmp(parsed.macro_labels[7], "PLAY") == 0);
    CHECK(parsed.macro_labels[1][0] == '\0');
    CHECK(parsed.show_graph == 1);
}

static void test_protocol_checksum_rejects_corruption(void)
{
    char labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN] = {0};
    device_config_packet built;
    protocol_build_device_config_packet(&built, labels, 1);

    uint8_t raw[sizeof(built)];
    memcpy(raw, &built, sizeof(raw));
    raw[3] ^= 0xFF; // flip a byte inside the body

    device_config_packet parsed;
    CHECK(!protocol_parse_device_config_packet(raw, sizeof(raw), &parsed));
}

static void test_protocol_reader_feed_roundtrip(void)
{
    char labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN] = {0};
    strcpy(labels[2], "SW3");
    device_config_packet built;
    protocol_build_device_config_packet(&built, labels, 0);

    protocol_reader_t reader;
    protocol_reader_init(&reader);

    const uint8_t *bytes = (const uint8_t *)&built;
    uint8_t out_type = 0;
    size_t out_size = 0;
    protocol_feed_result_t result = PROTOCOL_FEED_INCOMPLETE;

    for (size_t i = 0; i < sizeof(built); i++) {
        result = protocol_reader_feed(&reader, bytes[i], &out_type, &out_size);
        if (i + 1 < sizeof(built)) {
            CHECK(result == PROTOCOL_FEED_INCOMPLETE);
        }
    }

    CHECK(result == PROTOCOL_FEED_COMPLETE);
    CHECK(out_type == PROTOCOL_PACKET_DEVICE_CONFIG);
    CHECK(out_size == sizeof(built));

    device_config_packet parsed;
    CHECK(protocol_parse_device_config_packet(reader.buffer, out_size, &parsed));
    CHECK(strcmp(parsed.macro_labels[2], "SW3") == 0);
}

static void test_protocol_reader_resyncs_after_bad_checksum(void)
{
    command_event_packet good;
    protocol_build_command_event_packet(PROTOCOL_CMD_SWITCH_5, &good);

    uint8_t corrupt[sizeof(good)];
    memcpy(corrupt, &good, sizeof(corrupt));
    corrupt[sizeof(corrupt) - 1] ^= 0xFF; // wrong checksum

    protocol_reader_t reader;
    protocol_reader_init(&reader);

    uint8_t out_type = 0;
    size_t out_size = 0;
    protocol_feed_result_t result = PROTOCOL_FEED_INCOMPLETE;

    // Feed the corrupted packet -- expect BAD_CHECKSUM on the final byte.
    for (size_t i = 0; i < sizeof(corrupt); i++) {
        result = protocol_reader_feed(&reader, corrupt[i], &out_type, &out_size);
    }
    CHECK(result == PROTOCOL_FEED_BAD_CHECKSUM);

    // Reader must have resynced: a valid packet fed right after still parses.
    const uint8_t *good_bytes = (const uint8_t *)&good;
    for (size_t i = 0; i < sizeof(good); i++) {
        result = protocol_reader_feed(&reader, good_bytes[i], &out_type, &out_size);
    }
    CHECK(result == PROTOCOL_FEED_COMPLETE);
    CHECK(out_type == PROTOCOL_PACKET_COMMAND_EVENT);
}

// --- macro_map.c -------------------------------------------------------------

static void test_macro_map_defaults(void)
{
    macro_map_t *map = macro_map_create();

    for (int i = 0; i < MIXER_CHANNELS; i++) {
        macro_action_t action = macro_map_get(map, MACRO_TRIGGER_ENCODER_1_BTN + i);
        CHECK(action.type == MACRO_ACTION_TOGGLE_MUTE_SLOT);
        CHECK(action.target_slot == i);
    }

    for (int i = 0; i < MACRO_TRIGGER_SWITCH_8 + 1; i++) {
        macro_action_t action = macro_map_get(map, (macro_trigger_t)i);
        CHECK(action.type == MACRO_ACTION_NONE);
    }

    macro_map_destroy(map);
}

static void test_macro_map_set_get_roundtrip(void)
{
    macro_map_t *map = macro_map_create();

    macro_action_t action = {.type = MACRO_ACTION_SEND_KEYSTROKE, .target_slot = -1};
    action.steps[0] = (macro_keystroke_step_t){.modifiers = 0x1, .key = 5};
    action.step_count = 1;
    macro_map_set(map, MACRO_TRIGGER_SWITCH_3, action);

    macro_action_t got = macro_map_get(map, MACRO_TRIGGER_SWITCH_3);
    CHECK(got.type == MACRO_ACTION_SEND_KEYSTROKE);
    CHECK(got.step_count == 1);
    CHECK(got.steps[0].key == 5);
    CHECK(got.steps[0].modifiers == 0x1);

    macro_map_destroy(map);
}

static void test_macro_map_out_of_range_is_noop(void)
{
    macro_map_t *map = macro_map_create();

    macro_action_t bogus = {.type = MACRO_ACTION_LOG, .target_slot = 0};
    macro_map_set(map, (macro_trigger_t)-1, bogus);
    macro_map_set(map, MACRO_TRIGGER_COUNT, bogus);

    macro_action_t out_of_range = macro_map_get(map, MACRO_TRIGGER_COUNT);
    CHECK(out_of_range.type == MACRO_ACTION_NONE);

    macro_map_destroy(map);
}

static void test_macro_trigger_from_command(void)
{
    macro_trigger_t trigger;

    CHECK(macro_trigger_from_command(PROTOCOL_CMD_SWITCH_1, &trigger));
    CHECK(trigger == MACRO_TRIGGER_SWITCH_1);

    CHECK(macro_trigger_from_command(PROTOCOL_CMD_ENCODER_4_BTN, &trigger));
    CHECK(trigger == MACRO_TRIGGER_ENCODER_4_BTN);

    // Encoder rotation isn't a discrete button -- device_link handles it
    // separately, so this must not resolve to a trigger.
    CHECK(!macro_trigger_from_command(PROTOCOL_CMD_ENCODER_1_PLUS, &trigger));
    CHECK(!macro_trigger_from_command(0xFF, &trigger));
}

// --- device_settings.c -------------------------------------------------------

static void test_device_settings_defaults(void)
{
    device_settings_t *settings = device_settings_create();
    CHECK(device_settings_get_show_graph(settings));

    char label[MACRO_LABEL_LEN];
    device_settings_get_macro_label(settings, 0, label, sizeof(label));
    CHECK(label[0] == '\0');

    device_settings_destroy(settings);
}

static void test_device_settings_label_roundtrip_and_truncation(void)
{
    device_settings_t *settings = device_settings_create();

    device_settings_set_macro_label(settings, 3, "PLAY");
    char label[MACRO_LABEL_LEN];
    device_settings_get_macro_label(settings, 3, label, sizeof(label));
    CHECK(strcmp(label, "PLAY") == 0);

    // MACRO_LABEL_LEN is 10 -- a longer label must be truncated but still
    // null-terminated, never overrun the fixed-size wire field.
    device_settings_set_macro_label(settings, 4, "WAY_TOO_LONG_LABEL");
    device_settings_get_macro_label(settings, 4, label, sizeof(label));
    CHECK(strlen(label) == MACRO_LABEL_LEN - 1);

    device_settings_destroy(settings);
}

static void test_device_settings_out_of_range_is_noop(void)
{
    device_settings_t *settings = device_settings_create();

    device_settings_set_macro_label(settings, -1, "X");
    device_settings_set_macro_label(settings, MACRO_BUTTON_COUNT, "X");

    char label[MACRO_LABEL_LEN] = "unchanged";
    device_settings_get_macro_label(settings, MACRO_BUTTON_COUNT, label, sizeof(label));
    CHECK(strcmp(label, "unchanged") == 0); // out-of-range get must not touch out

    device_settings_destroy(settings);
}

static void test_device_settings_build_packet(void)
{
    device_settings_t *settings = device_settings_create();
    device_settings_set_macro_label(settings, 0, "MUTE");
    device_settings_set_show_graph(settings, false);

    device_config_packet built;
    device_settings_build_packet(settings, &built);

    device_config_packet parsed;
    CHECK(protocol_parse_device_config_packet((const uint8_t *)&built, sizeof(built), &parsed));
    CHECK(strcmp(parsed.macro_labels[0], "MUTE") == 0);
    CHECK(parsed.show_graph == 0);

    device_settings_destroy(settings);
}

// --- mixer_state.c (fake audio_backend) --------------------------------------
//
// A test double for audio_backend_vtable_t: create/poll/destroy are no-ops,
// set_volume/set_muted record the call and return a configurable result, and
// the callbacks handed to set_callbacks() are exposed so a test can fire
// added/updated/removed events directly -- standing in for what a real
// backend would do from poll() or its own thread.

typedef struct {
    audio_session_cb on_added, on_updated, on_removed;
    void *user_data;
    bool set_volume_result, set_muted_result;
    uint32_t last_set_volume_session;
    float last_set_volume;
    uint32_t last_set_muted_session;
    bool last_set_muted;
} fake_backend_t;

static fake_backend_t g_fake;

static audio_backend_t *fake_create(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.set_volume_result = true;
    g_fake.set_muted_result = true;
    return (audio_backend_t *)&g_fake;
}

static void fake_destroy(audio_backend_t *backend) { (void)backend; }
static void fake_poll(audio_backend_t *backend) { (void)backend; }

static bool fake_set_volume(audio_backend_t *backend, uint32_t session_id, float volume)
{
    (void)backend;
    g_fake.last_set_volume_session = session_id;
    g_fake.last_set_volume = volume;
    return g_fake.set_volume_result;
}

static bool fake_set_muted(audio_backend_t *backend, uint32_t session_id, bool muted)
{
    (void)backend;
    g_fake.last_set_muted_session = session_id;
    g_fake.last_set_muted = muted;
    return g_fake.set_muted_result;
}

static void fake_set_callbacks(audio_backend_t *backend, audio_session_cb on_added,
                               audio_session_cb on_updated, audio_session_cb on_removed,
                               void *user_data)
{
    (void)backend;
    g_fake.on_added = on_added;
    g_fake.on_updated = on_updated;
    g_fake.on_removed = on_removed;
    g_fake.user_data = user_data;
}

static const audio_backend_vtable_t g_fake_vtable = {
    .create = fake_create,
    .destroy = fake_destroy,
    .poll = fake_poll,
    .set_volume = fake_set_volume,
    .set_muted = fake_set_muted,
    .set_callbacks = fake_set_callbacks,
};

static audio_session_t make_session(uint32_t id, const char *name, float volume, bool muted)
{
    audio_session_t s = {.id = id, .volume = volume, .peak = 0.0f, .muted = muted};
    snprintf(s.name, sizeof(s.name), "%s", name);
    return s;
}

static void test_mixer_state_session_list_tracks_added_and_removed(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(1, "Discord", 0.5f, false);
    audio_session_t s2 = make_session(2, "Spotify", 0.8f, false);
    g_fake.on_added(&s1, g_fake.user_data);
    g_fake.on_added(&s2, g_fake.user_data);

    audio_session_t out[8];
    size_t n = mixer_state_list_sessions(state, out, 8);
    CHECK(n == 2);

    g_fake.on_removed(&s1, g_fake.user_data);
    n = mixer_state_list_sessions(state, out, 8);
    CHECK(n == 1);
    CHECK(out[0].id == 2);

    mixer_state_destroy(state);
}

static void test_mixer_state_assign_and_clear_slot(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(42, "Game", 0.6f, false);
    g_fake.on_added(&s1, g_fake.user_data);

    CHECK(!mixer_state_assign_slot(state, 0, 999)); // unknown session id

    CHECK(mixer_state_assign_slot(state, 0, 42));
    mixer_slot_t slot;
    CHECK(mixer_state_get_slot(state, 0, &slot));
    CHECK(slot.assigned);
    CHECK(slot.session_id == 42);
    CHECK(slot.volume == 60);
    CHECK(strcmp(slot.name, "Game") == 0);

    CHECK(mixer_state_clear_slot(state, 0));
    CHECK(mixer_state_get_slot(state, 0, &slot));
    CHECK(!slot.assigned);

    mixer_state_destroy(state);
}

static void test_mixer_state_removed_session_clears_its_slot(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(7, "Browser", 0.3f, false);
    g_fake.on_added(&s1, g_fake.user_data);
    CHECK(mixer_state_assign_slot(state, 1, 7));

    g_fake.on_removed(&s1, g_fake.user_data);

    mixer_slot_t slot;
    CHECK(mixer_state_get_slot(state, 1, &slot));
    CHECK(!slot.assigned);

    mixer_state_destroy(state);
}

static void test_mixer_state_set_volume_updates_slot_and_backend(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(5, "Game", 0.2f, false);
    g_fake.on_added(&s1, g_fake.user_data);
    mixer_state_assign_slot(state, 0, 5);

    CHECK(mixer_state_set_slot_volume(state, 0, 75));
    CHECK(g_fake.last_set_volume_session == 5);
    CHECK(g_fake.last_set_volume > 0.74f && g_fake.last_set_volume < 0.76f);

    mixer_slot_t slot;
    mixer_state_get_slot(state, 0, &slot);
    CHECK(slot.volume == 75);

    // Backend rejecting the change must leave the cached slot volume as-is.
    g_fake.set_volume_result = false;
    CHECK(!mixer_state_set_slot_volume(state, 0, 10));
    mixer_state_get_slot(state, 0, &slot);
    CHECK(slot.volume == 75);

    mixer_state_destroy(state);
}

static void test_mixer_state_adjust_volume_clamps(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(9, "Music", 0.9f, false);
    g_fake.on_added(&s1, g_fake.user_data);
    mixer_state_assign_slot(state, 2, 9);
    mixer_state_set_slot_volume(state, 2, 95);

    CHECK(mixer_state_adjust_slot_volume(state, 2, 20)); // 95 + 20 -> clamp 100
    mixer_slot_t slot;
    mixer_state_get_slot(state, 2, &slot);
    CHECK(slot.volume == 100);

    mixer_state_set_slot_volume(state, 2, 5);
    CHECK(mixer_state_adjust_slot_volume(state, 2, -50)); // 5 - 50 -> clamp 0
    mixer_state_get_slot(state, 2, &slot);
    CHECK(slot.volume == 0);

    mixer_state_destroy(state);
}

static void test_mixer_state_toggle_mute(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(3, "Chat", 0.5f, false);
    g_fake.on_added(&s1, g_fake.user_data);
    mixer_state_assign_slot(state, 0, 3);

    CHECK(mixer_state_toggle_slot_mute(state, 0));
    CHECK(g_fake.last_set_muted_session == 3);
    CHECK(g_fake.last_set_muted == true);

    mixer_state_destroy(state);
}

static void test_mixer_state_build_packets_reflect_assignment(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(1, "Discord", 0.5f, false);
    g_fake.on_added(&s1, g_fake.user_data);
    mixer_state_assign_slot(state, 0, 1);
    mixer_state_set_slot_volume(state, 0, 50);

    levels_packet levels;
    mixer_state_build_levels_packet(state, &levels);
    CHECK(levels.valid == 1);
    CHECK(levels.channels[0].volume == 50);
    CHECK(levels.channels[1].volume == 0); // unassigned slot

    metadata_packet metadata;
    mixer_state_build_metadata_packet(state, &metadata);
    CHECK(strcmp(metadata.names[0], "Discord") == 0);
    CHECK(metadata.names[1][0] == '\0');

    mixer_state_destroy(state);
}

int main(void)
{
    RUN_TEST(test_protocol_levels_roundtrip);
    RUN_TEST(test_protocol_metadata_roundtrip);
    RUN_TEST(test_protocol_device_config_roundtrip);
    RUN_TEST(test_protocol_checksum_rejects_corruption);
    RUN_TEST(test_protocol_reader_feed_roundtrip);
    RUN_TEST(test_protocol_reader_resyncs_after_bad_checksum);

    RUN_TEST(test_macro_map_defaults);
    RUN_TEST(test_macro_map_set_get_roundtrip);
    RUN_TEST(test_macro_map_out_of_range_is_noop);
    RUN_TEST(test_macro_trigger_from_command);

    RUN_TEST(test_device_settings_defaults);
    RUN_TEST(test_device_settings_label_roundtrip_and_truncation);
    RUN_TEST(test_device_settings_out_of_range_is_noop);
    RUN_TEST(test_device_settings_build_packet);

    RUN_TEST(test_mixer_state_session_list_tracks_added_and_removed);
    RUN_TEST(test_mixer_state_assign_and_clear_slot);
    RUN_TEST(test_mixer_state_removed_session_clears_its_slot);
    RUN_TEST(test_mixer_state_set_volume_updates_slot_and_backend);
    RUN_TEST(test_mixer_state_adjust_volume_clamps);
    RUN_TEST(test_mixer_state_toggle_mute);
    RUN_TEST(test_mixer_state_build_packets_reflect_assignment);

    if (g_failures == 0) {
        printf("\nAll tests passed.\n");
        return 0;
    }
    printf("\n%d assertion(s) failed.\n", g_failures);
    return 1;
}
