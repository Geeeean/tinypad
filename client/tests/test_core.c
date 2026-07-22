// Minimal assert-based test runner for the platform-independent core/
// modules (protocol framing, macro_map, device_settings, mixer_state).
// No framework dependency, matching the rest of this project's style --
// each RUN_TEST prints a line and aborts on the first failure so a red run
// points straight at the offending assertion.

#include "core/device_discovery.h"
#include "core/device_settings.h"
#include "core/macro_map.h"
#include "core/mixer_state.h"
#include "core/profile_store.h"
#include "platform/audio_backend.h"
#include "platform/audio_simulated.h"
#include "protocol.h"

#include <math.h>
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
    protocol_build_levels_packet(&built, channels);

    levels_packet parsed;
    CHECK(PROTOCOL_PARSE(PROTOCOL_PACKET_LEVELS, (const uint8_t *)&built, sizeof(built), &parsed));
    CHECK(parsed.hdr.header == PROTOCOL_START_BYTE);
    CHECK(parsed.hdr.type == PROTOCOL_PACKET_LEVELS);
    CHECK(memcmp(parsed.channels, channels, sizeof(channels)) == 0);
}

static void test_protocol_metadata_roundtrip(void)
{
    char names[MIXER_CHANNELS][CHANNEL_NAME_LEN] = {"Discord", "Spotify", "Game", "Browser"};
    metadata_packet built;
    protocol_build_metadata_packet(&built, names);

    metadata_packet parsed;
    CHECK(PROTOCOL_PARSE(PROTOCOL_PACKET_METADATA, (const uint8_t *)&built, sizeof(built), &parsed));
    CHECK(strcmp(parsed.names[0], "Discord") == 0);
    CHECK(strcmp(parsed.names[3], "Browser") == 0);
}

static void test_protocol_device_config_roundtrip(void)
{
    char labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN] = {0};
    strcpy(labels[0], "MUTE");
    strcpy(labels[7], "PLAY");
    uint8_t gui_layout[GUI_COMPONENT_COUNT] = {GUI_COMPONENT_VU_METERS, GUI_COMPONENT_WAVEFORM,
                                               GUI_COMPONENT_MACRO_GRID};

    device_config_packet built;
    protocol_build_device_config_packet(&built, labels, gui_layout);

    device_config_packet parsed;
    CHECK(PROTOCOL_PARSE(PROTOCOL_PACKET_DEVICE_CONFIG, (const uint8_t *)&built, sizeof(built),
                         &parsed));
    CHECK(strcmp(parsed.macro_labels[0], "MUTE") == 0);
    CHECK(strcmp(parsed.macro_labels[7], "PLAY") == 0);
    CHECK(parsed.macro_labels[1][0] == '\0');
    CHECK(memcmp(parsed.gui_layout, gui_layout, sizeof(gui_layout)) == 0);
}

static void test_protocol_checksum_rejects_corruption(void)
{
    char labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN] = {0};
    uint8_t gui_layout[GUI_COMPONENT_COUNT] = {GUI_COMPONENT_VU_METERS, GUI_COMPONENT_WAVEFORM,
                                               GUI_COMPONENT_MACRO_GRID};
    device_config_packet built;
    protocol_build_device_config_packet(&built, labels, gui_layout);

    uint8_t raw[sizeof(built)];
    memcpy(raw, &built, sizeof(raw));
    raw[3] ^= 0xFF; // flip a byte inside the body

    device_config_packet parsed;
    CHECK(!PROTOCOL_PARSE(PROTOCOL_PACKET_DEVICE_CONFIG, raw, sizeof(raw), &parsed));
}

static void test_protocol_reader_feed_roundtrip(void)
{
    char labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN] = {0};
    strcpy(labels[2], "SW3");
    uint8_t gui_layout[GUI_COMPONENT_COUNT] = {GUI_COMPONENT_MACRO_GRID, GUI_COMPONENT_NONE,
                                               GUI_COMPONENT_NONE};
    device_config_packet built;
    protocol_build_device_config_packet(&built, labels, gui_layout);

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
    CHECK(PROTOCOL_PARSE(PROTOCOL_PACKET_DEVICE_CONFIG, reader.buffer, out_size, &parsed));
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

static void test_protocol_normalize_gui_layout_clears_bad_entries(void)
{
    uint8_t layout[GUI_COMPONENT_COUNT] = {GUI_COMPONENT_MACRO_GRID, GUI_COMPONENT_MACRO_GRID,
                                           0x7F};
    protocol_normalize_gui_layout(layout);
    CHECK(layout[0] == GUI_COMPONENT_MACRO_GRID); // first occurrence kept
    CHECK(layout[1] == GUI_COMPONENT_NONE);       // duplicate cleared
    CHECK(layout[2] == GUI_COMPONENT_NONE);       // out-of-range cleared

    uint8_t untouched[GUI_COMPONENT_COUNT] = {GUI_COMPONENT_NONE, GUI_COMPONENT_WAVEFORM,
                                              GUI_COMPONENT_VU_METERS};
    protocol_normalize_gui_layout(untouched);
    CHECK(untouched[0] == GUI_COMPONENT_NONE);
    CHECK(untouched[1] == GUI_COMPONENT_WAVEFORM);
    CHECK(untouched[2] == GUI_COMPONENT_VU_METERS);
}

// --- device_discovery.c -------------------------------------------------------

static serial_port_info_t make_port_info(uint16_t vendor_id, uint16_t product_id,
                                         const char *product)
{
    serial_port_info_t info = {0};
    info.vendor_id = vendor_id;
    info.product_id = product_id;
    if (product) {
        snprintf(info.product, sizeof(info.product), "%s", product);
    }
    snprintf(info.path, sizeof(info.path), "/dev/fake");
    return info;
}

static void test_device_discovery_matches_by_vendor_and_product_string(void)
{
    serial_port_info_t info =
        make_port_info(TINYPAD_USB_VENDOR_ID, 0x9999, TINYPAD_USB_PRODUCT_STRING);
    CHECK(device_discovery_matches(&info));

    // Product string wins over a mismatched PID -- it's the more robust
    // signal (PID shifts if firmware ever enables another USB class).
    serial_port_info_t wrong_product =
        make_port_info(TINYPAD_USB_VENDOR_ID, TINYPAD_USB_PRODUCT_ID, "Some Other Device");
    CHECK(!device_discovery_matches(&wrong_product));
}

static void test_device_discovery_falls_back_to_pid_without_product_string(void)
{
    serial_port_info_t info = make_port_info(TINYPAD_USB_VENDOR_ID, TINYPAD_USB_PRODUCT_ID, NULL);
    CHECK(device_discovery_matches(&info));

    serial_port_info_t wrong_pid = make_port_info(TINYPAD_USB_VENDOR_ID, 0x9999, NULL);
    CHECK(!device_discovery_matches(&wrong_pid));
}

static void test_device_discovery_rejects_wrong_vendor(void)
{
    // Even a matching PID/product string must not match under the wrong
    // VID -- other vendors can and do reuse PIDs.
    serial_port_info_t info = make_port_info(0x1234, TINYPAD_USB_PRODUCT_ID,
                                             TINYPAD_USB_PRODUCT_STRING);
    CHECK(!device_discovery_matches(&info));
}

static void test_device_discovery_find_scans_candidate_list(void)
{
    CHECK(device_discovery_find(NULL, 0) == -1);

    serial_port_info_t ports[3] = {
        make_port_info(0x1234, 0x0001, "Unrelated Device"),
        make_port_info(TINYPAD_USB_VENDOR_ID, TINYPAD_USB_PRODUCT_ID, TINYPAD_USB_PRODUCT_STRING),
        make_port_info(0x5678, 0x0002, "Another Device"),
    };
    CHECK(device_discovery_find(ports, 3) == 1);

    serial_port_info_t no_match[2] = {
        make_port_info(0x1234, 0x0001, "Unrelated Device"),
        make_port_info(0x5678, 0x0002, "Another Device"),
    };
    CHECK(device_discovery_find(no_match, 2) == -1);
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

    uint8_t gui_layout[GUI_COMPONENT_COUNT];
    device_settings_get_gui_layout(settings, gui_layout);
    uint8_t expected[GUI_COMPONENT_COUNT] = {GUI_COMPONENT_VU_METERS, GUI_COMPONENT_WAVEFORM,
                                             GUI_COMPONENT_MACRO_GRID};
    CHECK(memcmp(gui_layout, expected, sizeof(expected)) == 0);

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
    uint8_t gui_layout[GUI_COMPONENT_COUNT] = {GUI_COMPONENT_MACRO_GRID, GUI_COMPONENT_VU_METERS,
                                               GUI_COMPONENT_NONE};
    device_settings_set_gui_layout(settings, gui_layout);

    device_config_packet built;
    device_settings_build_packet(settings, &built);

    device_config_packet parsed;
    CHECK(PROTOCOL_PARSE(PROTOCOL_PACKET_DEVICE_CONFIG, (const uint8_t *)&built, sizeof(built),
                         &parsed));
    CHECK(strcmp(parsed.macro_labels[0], "MUTE") == 0);
    CHECK(memcmp(parsed.gui_layout, gui_layout, sizeof(gui_layout)) == 0);

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

    // Fake "system output" -- see audio_backend_vtable_t's get_master()/
    // set_master_volume()/set_master_muted(). has_master toggles whether
    // get_master() reports one at all (a backend without master support
    // leaves these NULL; has_master=false here stands in for that).
    bool has_master;
    float master_volume, master_peak;
    bool master_muted;
    bool set_master_volume_result, set_master_muted_result;
    float last_set_master_volume;
    bool last_set_master_muted;
    int get_master_calls;
} fake_backend_t;

static fake_backend_t g_fake;

static audio_backend_t *fake_create(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.set_volume_result = true;
    g_fake.set_muted_result = true;
    g_fake.has_master = true;
    g_fake.master_volume = 1.0f;
    g_fake.set_master_volume_result = true;
    g_fake.set_master_muted_result = true;
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

static bool fake_get_master(audio_backend_t *backend, audio_session_t *out)
{
    (void)backend;
    g_fake.get_master_calls++;
    if (!g_fake.has_master) {
        return false;
    }
    out->id = 0; // ignored by mixer_state_poll(), which substitutes its own sentinel
    snprintf(out->name, sizeof(out->name), "Master");
    out->volume = g_fake.master_volume;
    out->peak = g_fake.master_peak;
    out->muted = g_fake.master_muted;
    return true;
}

static bool fake_set_master_volume(audio_backend_t *backend, float volume)
{
    (void)backend;
    g_fake.last_set_master_volume = volume;
    return g_fake.set_master_volume_result;
}

static bool fake_set_master_muted(audio_backend_t *backend, bool muted)
{
    (void)backend;
    g_fake.last_set_master_muted = muted;
    return g_fake.set_master_muted_result;
}

static const audio_backend_vtable_t g_fake_vtable = {
    .create = fake_create,
    .destroy = fake_destroy,
    .poll = fake_poll,
    .set_volume = fake_set_volume,
    .set_muted = fake_set_muted,
    .set_callbacks = fake_set_callbacks,
    .get_master = fake_get_master,
    .set_master_volume = fake_set_master_volume,
    .set_master_muted = fake_set_master_muted,
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

static void test_mixer_state_assign_master_slot(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    // No on_added needed -- MIXER_MASTER_SESSION_ID isn't looked up in the
    // session table.
    CHECK(mixer_state_assign_slot(state, 1, MIXER_MASTER_SESSION_ID));

    mixer_slot_t slot;
    mixer_state_get_slot(state, 1, &slot);
    CHECK(slot.assigned);
    CHECK(slot.session_id == MIXER_MASTER_SESSION_ID);

    mixer_state_destroy(state);
}

static void test_mixer_state_poll_syncs_master_into_slot(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);
    mixer_state_assign_slot(state, 0, MIXER_MASTER_SESSION_ID);

    g_fake.master_volume = 0.4f;
    g_fake.master_peak = 0.6f;
    g_fake.master_muted = true;
    mixer_state_poll(state);

    mixer_slot_t slot;
    mixer_state_get_slot(state, 0, &slot);
    CHECK(slot.volume == 40);
    CHECK(slot.peak == 60);
    CHECK(slot.muted == true);

    mixer_state_destroy(state);
}

static void test_mixer_state_poll_ignores_master_when_unsupported(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);
    mixer_state_assign_slot(state, 0, MIXER_MASTER_SESSION_ID);

    g_fake.has_master = false;
    mixer_state_poll(state); // must not crash/misbehave when get_master() fails

    mixer_slot_t slot;
    mixer_state_get_slot(state, 0, &slot);
    CHECK(slot.assigned); // still master-bound, just not refreshed this tick

    mixer_state_destroy(state);
}

static void test_mixer_state_set_volume_routes_to_master(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);
    mixer_state_assign_slot(state, 2, MIXER_MASTER_SESSION_ID);

    CHECK(mixer_state_set_slot_volume(state, 2, 65));
    CHECK(g_fake.last_set_master_volume > 0.64f && g_fake.last_set_master_volume < 0.66f);
    // Must not also go through the per-session path.
    CHECK(g_fake.last_set_volume_session == 0);

    mixer_slot_t slot;
    mixer_state_get_slot(state, 2, &slot);
    CHECK(slot.volume == 65);

    mixer_state_destroy(state);
}

static void test_mixer_state_toggle_mute_routes_to_master(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);
    mixer_state_assign_slot(state, 3, MIXER_MASTER_SESSION_ID);

    CHECK(mixer_state_toggle_slot_mute(state, 3));
    CHECK(g_fake.last_set_master_muted == true);

    mixer_slot_t slot;
    mixer_state_get_slot(state, 3, &slot);
    CHECK(slot.muted == true);

    // Toggling again flips back, based on the slot's own last-synced state
    // (master isn't in state->sessions[], unlike the per-app path).
    CHECK(mixer_state_toggle_slot_mute(state, 3));
    CHECK(g_fake.last_set_master_muted == false);

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
    CHECK(levels.channels[0].volume == 50);
    CHECK(levels.channels[1].volume == 0); // unassigned slot

    metadata_packet metadata;
    mixer_state_build_metadata_packet(state, &metadata);
    CHECK(strcmp(metadata.names[0], "Discord") == 0);
    CHECK(metadata.names[1][0] == '\0');

    mixer_state_destroy(state);
}

// g_fake is a single global reused across mixer_state_create()/
// mixer_state_set_backend() calls -- fake_create() memsets it fresh each
// time it's invoked, mirroring how a real second backend instance would
// start with no sessions of its own.
static void test_mixer_state_set_backend_clears_sessions_and_slots(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(1, "Discord", 0.5f, false);
    g_fake.on_added(&s1, g_fake.user_data);
    CHECK(mixer_state_assign_slot(state, 0, 1));

    mixer_state_set_backend(state, &g_fake_vtable);

    audio_session_t out[8];
    CHECK(mixer_state_list_sessions(state, out, 8) == 0);

    mixer_slot_t slot;
    CHECK(mixer_state_get_slot(state, 0, &slot));
    CHECK(!slot.assigned);

    // The new backend's callbacks are still wired up -- a session it
    // reports should flow through normally.
    audio_session_t s2 = make_session(2, "Spotify", 0.7f, false);
    g_fake.on_added(&s2, g_fake.user_data);
    CHECK(mixer_state_list_sessions(state, out, 8) == 1);

    mixer_state_destroy(state);
}

static void test_mixer_state_pending_assignment_immediate(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    audio_session_t s1 = make_session(1, "Discord", 0.5f, false);
    g_fake.on_added(&s1, g_fake.user_data);

    mixer_state_set_pending_assignment(state, 0, "Discord");

    mixer_slot_t slot;
    CHECK(mixer_state_get_slot(state, 0, &slot));
    CHECK(slot.assigned);
    CHECK(slot.session_id == 1);

    mixer_state_destroy(state);
}

static void test_mixer_state_pending_assignment_on_later_add(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    mixer_state_set_pending_assignment(state, 1, "Spotify");

    mixer_slot_t slot;
    CHECK(mixer_state_get_slot(state, 1, &slot));
    CHECK(!slot.assigned); // not running yet

    audio_session_t s = make_session(7, "Spotify", 0.3f, false);
    g_fake.on_added(&s, g_fake.user_data);

    CHECK(mixer_state_get_slot(state, 1, &slot));
    CHECK(slot.assigned);
    CHECK(slot.session_id == 7);

    mixer_state_destroy(state);
}

static void test_mixer_state_pending_assignment_cancelled_by_manual_assign(void)
{
    mixer_state_t *state = mixer_state_create(&g_fake_vtable);

    mixer_state_set_pending_assignment(state, 2, "Chrome");

    // User manually assigns a different session to the same slot before
    // "Chrome" ever shows up.
    audio_session_t other = make_session(9, "Firefox", 0.4f, false);
    g_fake.on_added(&other, g_fake.user_data);
    CHECK(mixer_state_assign_slot(state, 2, 9));

    // Now "Chrome" appears -- it must NOT steal the slot back.
    audio_session_t chrome = make_session(10, "Chrome", 0.6f, false);
    g_fake.on_added(&chrome, g_fake.user_data);

    mixer_slot_t slot;
    CHECK(mixer_state_get_slot(state, 2, &slot));
    CHECK(slot.assigned);
    CHECK(slot.session_id == 9); // still Firefox, not hijacked by the late Chrome match

    mixer_state_destroy(state);
}

// --- audio_simulated.c -------------------------------------------------------

typedef struct {
    bool have[MIXER_CHANNELS];
    float peak[MIXER_CHANNELS];
} sim_capture_t;

static void sim_capture_cb(const audio_session_t *session, void *user_data)
{
    sim_capture_t *cap = user_data;
    uint32_t base = 0xF0000000u;
    if (session->id < base) {
        return;
    }
    uint32_t idx = session->id - base;
    if (idx >= MIXER_CHANNELS) {
        return;
    }
    cap->have[idx] = true;
    cap->peak[idx] = session->peak;
}

// Drives audio_simulated's fake peak generator through many poll() ticks and
// checks the "realistic, glitch-free" properties it's meant to have: every
// reported peak stays within [0, ceiling], no tick-to-tick value ever jumps
// by more than the easing filter's max possible step (i.e. always smooth,
// never a discontinuous spike/clip), it's not just stuck at zero, and two
// channels given the same ceiling still diverge (independent per-channel
// randomness, not four identical/lockstep meters).
static void test_audio_simulated_peaks_are_smooth_and_bounded(void)
{
    const audio_backend_vtable_t *vt = audio_simulated_get_vtable();
    audio_backend_t *backend = vt->create();
    CHECK(backend != NULL);

    sim_capture_t cap = {0};
    vt->set_callbacks(backend, sim_capture_cb, sim_capture_cb, NULL, &cap);

    vt->poll(backend); // first poll: announces all channels, still silent
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        CHECK(cap.have[i]);
        CHECK(cap.peak[i] == 0.0f);
    }

    const float ceiling = 0.80f;
    CHECK(audio_simulated_set_level(0, (uint8_t)(ceiling * 100)));
    CHECK(audio_simulated_set_level(1, (uint8_t)(ceiling * 100)));

    float prev0 = 0.0f, prev1 = 0.0f;
    bool saw_activity = false;
    bool diverged = false;
    for (int tick = 0; tick < 300; tick++) {
        vt->poll(backend);

        CHECK(cap.peak[0] >= 0.0f && cap.peak[0] <= ceiling + 1e-3f);
        CHECK(cap.peak[1] >= 0.0f && cap.peak[1] <= ceiling + 1e-3f);
        CHECK(fabsf(cap.peak[0] - prev0) <= 0.16f);
        CHECK(fabsf(cap.peak[1] - prev1) <= 0.16f);

        if (cap.peak[0] > 0.05f) {
            saw_activity = true;
        }
        if (fabsf(cap.peak[0] - cap.peak[1]) > 0.02f) {
            diverged = true;
        }
        prev0 = cap.peak[0];
        prev1 = cap.peak[1];
    }
    CHECK(saw_activity);
    CHECK(diverged);

    // Muting eases the peak down to silence rather than snapping.
    CHECK(vt->set_muted(backend, 0xF0000000u, true));
    float prev = prev0;
    bool ever_jumped = false;
    for (int tick = 0; tick < 60; tick++) {
        vt->poll(backend);
        if (fabsf(cap.peak[0] - prev) > 0.16f) {
            ever_jumped = true;
        }
        prev = cap.peak[0];
    }
    CHECK(!ever_jumped);
    CHECK(cap.peak[0] < 0.05f); // settled near silent after enough ticks

    vt->destroy(backend);
}

// Reproduces exactly what ui_bridge.c's native_set_simulation_enabled does
// after swapping in the simulated backend: force one synchronous poll (so
// the sessions exist immediately rather than waiting for a background
// thread's next tick) and assign each simulated channel straight to its
// matching slot. Guards the fix for "enabling simulation shows 0 on every
// VU meter because nothing is assigned yet".
static void test_mixer_state_with_simulated_backend_auto_assigns(void)
{
    mixer_state_t *state = mixer_state_create(audio_simulated_get_vtable());

    mixer_state_poll(state);
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        CHECK(mixer_state_assign_slot(state, i, audio_simulated_session_id(i)));
    }

    for (int i = 0; i < MIXER_CHANNELS; i++) {
        mixer_slot_t slot;
        CHECK(mixer_state_get_slot(state, i, &slot));
        CHECK(slot.assigned);
        CHECK(slot.session_id == audio_simulated_session_id(i));
    }

    // Default level is non-zero (audio_simulated.c's sim_create()), so
    // after enough polls at least one slot should show real peak activity
    // reaching the wire packet -- not stuck at 0.
    bool saw_nonzero_peak = false;
    for (int tick = 0; tick < 120; tick++) {
        mixer_state_poll(state);
        levels_packet levels;
        mixer_state_build_levels_packet(state, &levels);
        for (int i = 0; i < MIXER_CHANNELS; i++) {
            if (levels.channels[i].left > 0) {
                saw_nonzero_peak = true;
            }
        }
    }
    CHECK(saw_nonzero_peak);

    mixer_state_destroy(state);
}

// --- profile_store.c ---------------------------------------------------------
// ":memory:" -- no file I/O, matches this suite's no-external-resources style.

static void test_profile_store_seeds_default_on_first_open(void)
{
    profile_store_t *store = profile_store_open(":memory:");
    CHECK(store != NULL);

    profile_summary_t profiles[8];
    size_t n = profile_store_list(store, profiles, 8);
    CHECK(n == 1);
    CHECK(strcmp(profiles[0].name, "Default") == 0);

    int64_t active;
    CHECK(profile_store_get_active_id(store, &active));
    CHECK(active == profiles[0].id);

    profile_store_close(store);
}

static void test_profile_store_save_as_and_load_round_trip(void)
{
    profile_store_t *store = profile_store_open(":memory:");
    macro_map_t *macros = macro_map_create();
    device_settings_t *settings = device_settings_create();

    device_settings_set_macro_label(settings, 3, "MUTE ALL");
    uint8_t layout[GUI_COMPONENT_COUNT] = {GUI_COMPONENT_MACRO_GRID, GUI_COMPONENT_NONE,
                                           GUI_COMPONENT_NONE};
    device_settings_set_gui_layout(settings, layout);
    macro_action_t action = {.type = MACRO_ACTION_TOGGLE_MUTE_SLOT, .target_slot = 2};
    macro_map_set(macros, MACRO_TRIGGER_SWITCH_5, action);

    int64_t new_id;
    CHECK(profile_store_save_as(store, "Streaming", macros, settings, NULL, &new_id));

    profile_summary_t profiles[8];
    CHECK(profile_store_list(store, profiles, 8) == 2);

    int64_t active;
    CHECK(profile_store_get_active_id(store, &active));
    CHECK(active == new_id); // save_as makes the new profile active

    macro_map_t *macros2 = macro_map_create();
    device_settings_t *settings2 = device_settings_create();
    CHECK(profile_store_load(store, new_id, macros2, settings2, NULL));

    char label[MACRO_LABEL_LEN];
    device_settings_get_macro_label(settings2, 3, label, sizeof(label));
    CHECK(strcmp(label, "MUTE ALL") == 0);

    uint8_t got_layout[GUI_COMPONENT_COUNT];
    device_settings_get_gui_layout(settings2, got_layout);
    CHECK(got_layout[0] == GUI_COMPONENT_MACRO_GRID);
    CHECK(got_layout[1] == GUI_COMPONENT_NONE);

    macro_action_t got = macro_map_get(macros2, MACRO_TRIGGER_SWITCH_5);
    CHECK(got.type == MACRO_ACTION_TOGGLE_MUTE_SLOT);
    CHECK(got.target_slot == 2);

    macro_map_destroy(macros);
    macro_map_destroy(macros2);
    device_settings_destroy(settings);
    device_settings_destroy(settings2);
    profile_store_close(store);
}

static void test_profile_store_round_trips_keystroke_steps(void)
{
    profile_store_t *store = profile_store_open(":memory:");
    macro_map_t *macros = macro_map_create();
    device_settings_t *settings = device_settings_create();

    macro_action_t action = {.type = MACRO_ACTION_SEND_KEYSTROKE, .target_slot = -1};
    action.steps[0] = (macro_keystroke_step_t){.modifiers = MACRO_MOD_META, .key = MACRO_KEY_C};
    action.steps[1] = (macro_keystroke_step_t){.modifiers = 0, .key = MACRO_KEY_V};
    action.step_count = 2;
    macro_map_set(macros, MACRO_TRIGGER_SWITCH_1, action);

    int64_t new_id;
    CHECK(profile_store_save_as(store, "Keystrokes", macros, settings, NULL, &new_id));

    macro_map_t *macros2 = macro_map_create();
    device_settings_t *settings2 = device_settings_create();
    CHECK(profile_store_load(store, new_id, macros2, settings2, NULL));

    macro_action_t got = macro_map_get(macros2, MACRO_TRIGGER_SWITCH_1);
    CHECK(got.type == MACRO_ACTION_SEND_KEYSTROKE);
    CHECK(got.step_count == 2);
    CHECK(got.steps[0].modifiers == MACRO_MOD_META);
    CHECK(got.steps[0].key == MACRO_KEY_C);
    CHECK(got.steps[1].modifiers == 0);
    CHECK(got.steps[1].key == MACRO_KEY_V);

    macro_map_destroy(macros);
    macro_map_destroy(macros2);
    device_settings_destroy(settings);
    device_settings_destroy(settings2);
    profile_store_close(store);
}

static void test_profile_store_save_overwrites_active_profile(void)
{
    profile_store_t *store = profile_store_open(":memory:");
    macro_map_t *macros = macro_map_create();
    device_settings_t *settings = device_settings_create();

    int64_t active;
    CHECK(profile_store_get_active_id(store, &active)); // "Default"

    device_settings_set_macro_label(settings, 0, "HELLO");
    CHECK(profile_store_save(store, active, macros, settings, NULL));

    macro_map_t *macros2 = macro_map_create();
    device_settings_t *settings2 = device_settings_create();
    CHECK(profile_store_load(store, active, macros2, settings2, NULL));

    char label[MACRO_LABEL_LEN];
    device_settings_get_macro_label(settings2, 0, label, sizeof(label));
    CHECK(strcmp(label, "HELLO") == 0);

    // Still just one profile -- save() overwrote in place, didn't create a
    // new row.
    profile_summary_t profiles[8];
    CHECK(profile_store_list(store, profiles, 8) == 1);

    macro_map_destroy(macros);
    macro_map_destroy(macros2);
    device_settings_destroy(settings);
    device_settings_destroy(settings2);
    profile_store_close(store);
}

static void test_profile_store_rename(void)
{
    profile_store_t *store = profile_store_open(":memory:");
    profile_summary_t profiles[8];
    profile_store_list(store, profiles, 8);

    CHECK(profile_store_rename(store, profiles[0].id, "My Setup"));
    profile_store_list(store, profiles, 8);
    CHECK(strcmp(profiles[0].name, "My Setup") == 0);

    CHECK(!profile_store_rename(store, 99999, "Ghost")); // nonexistent profile

    profile_store_close(store);
}

static void test_profile_store_delete_refuses_last_and_cascades(void)
{
    profile_store_t *store = profile_store_open(":memory:");
    macro_map_t *macros = macro_map_create();
    device_settings_t *settings = device_settings_create();

    macro_action_t action = {.type = MACRO_ACTION_SEND_KEYSTROKE, .target_slot = -1};
    action.steps[0] = (macro_keystroke_step_t){.modifiers = 0, .key = MACRO_KEY_A};
    action.step_count = 1;
    macro_map_set(macros, MACRO_TRIGGER_SWITCH_1, action);

    int64_t new_id;
    CHECK(profile_store_save_as(store, "Temp", macros, settings, NULL, &new_id));

    profile_summary_t profiles[8];
    CHECK(profile_store_list(store, profiles, 8) == 2);

    // Two profiles exist -- deleting one (with keystroke-step child rows,
    // exercising the ON DELETE CASCADE chain) should succeed.
    CHECK(profile_store_delete(store, new_id));
    CHECK(profile_store_list(store, profiles, 8) == 1);

    // Only one left now -- refuse to delete the last remaining profile.
    CHECK(!profile_store_delete(store, profiles[0].id));
    CHECK(profile_store_list(store, profiles, 8) == 1);

    macro_map_destroy(macros);
    device_settings_destroy(settings);
    profile_store_close(store);
}

static void test_profile_store_delete_active_falls_back(void)
{
    profile_store_t *store = profile_store_open(":memory:");
    macro_map_t *macros = macro_map_create();
    device_settings_t *settings = device_settings_create();

    profile_summary_t profiles[8];
    profile_store_list(store, profiles, 8);
    int64_t default_id = profiles[0].id;

    int64_t new_id;
    CHECK(profile_store_save_as(store, "Temp", macros, settings, NULL, &new_id)); // becomes active

    int64_t active;
    CHECK(profile_store_get_active_id(store, &active));
    CHECK(active == new_id);

    CHECK(profile_store_delete(store, new_id));

    CHECK(profile_store_get_active_id(store, &active));
    CHECK(active == default_id); // fell back to the remaining profile

    macro_map_destroy(macros);
    device_settings_destroy(settings);
    profile_store_close(store);
}

static void test_profile_store_persists_and_reconnects_slot_assignment(void)
{
    profile_store_t *store = profile_store_open(":memory:");
    macro_map_t *macros = macro_map_create();
    device_settings_t *settings = device_settings_create();
    mixer_state_t *mixer = mixer_state_create(&g_fake_vtable);

    audio_session_t s = make_session(5, "Discord", 0.5f, false);
    g_fake.on_added(&s, g_fake.user_data);
    CHECK(mixer_state_assign_slot(mixer, 1, 5));

    int64_t new_id;
    CHECK(profile_store_save_as(store, "WithSlots", macros, settings, mixer, &new_id));

    // Fresh mixer_state, no sessions known yet -- load should leave the
    // slot unassigned until "Discord" actually appears.
    macro_map_t *macros2 = macro_map_create();
    device_settings_t *settings2 = device_settings_create();
    mixer_state_t *mixer2 = mixer_state_create(&g_fake_vtable); // rewires g_fake to mixer2
    CHECK(profile_store_load(store, new_id, macros2, settings2, mixer2));

    mixer_slot_t slot;
    CHECK(mixer_state_get_slot(mixer2, 1, &slot));
    CHECK(!slot.assigned);

    audio_session_t s2 = make_session(11, "Discord", 0.2f, false);
    g_fake.on_added(&s2, g_fake.user_data); // fires against mixer2

    CHECK(mixer_state_get_slot(mixer2, 1, &slot));
    CHECK(slot.assigned);
    CHECK(slot.session_id == 11);

    mixer_state_destroy(mixer);
    mixer_state_destroy(mixer2);
    macro_map_destroy(macros);
    macro_map_destroy(macros2);
    device_settings_destroy(settings);
    device_settings_destroy(settings2);
    profile_store_close(store);
}

int main(void)
{
    RUN_TEST(test_protocol_levels_roundtrip);
    RUN_TEST(test_protocol_metadata_roundtrip);
    RUN_TEST(test_protocol_device_config_roundtrip);
    RUN_TEST(test_protocol_checksum_rejects_corruption);
    RUN_TEST(test_protocol_reader_feed_roundtrip);
    RUN_TEST(test_protocol_reader_resyncs_after_bad_checksum);
    RUN_TEST(test_protocol_normalize_gui_layout_clears_bad_entries);

    RUN_TEST(test_device_discovery_matches_by_vendor_and_product_string);
    RUN_TEST(test_device_discovery_falls_back_to_pid_without_product_string);
    RUN_TEST(test_device_discovery_rejects_wrong_vendor);
    RUN_TEST(test_device_discovery_find_scans_candidate_list);

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
    RUN_TEST(test_mixer_state_assign_master_slot);
    RUN_TEST(test_mixer_state_poll_syncs_master_into_slot);
    RUN_TEST(test_mixer_state_poll_ignores_master_when_unsupported);
    RUN_TEST(test_mixer_state_set_volume_routes_to_master);
    RUN_TEST(test_mixer_state_toggle_mute_routes_to_master);
    RUN_TEST(test_mixer_state_build_packets_reflect_assignment);
    RUN_TEST(test_mixer_state_set_backend_clears_sessions_and_slots);
    RUN_TEST(test_mixer_state_pending_assignment_immediate);
    RUN_TEST(test_mixer_state_pending_assignment_on_later_add);
    RUN_TEST(test_mixer_state_pending_assignment_cancelled_by_manual_assign);

    RUN_TEST(test_audio_simulated_peaks_are_smooth_and_bounded);
    RUN_TEST(test_mixer_state_with_simulated_backend_auto_assigns);

    RUN_TEST(test_profile_store_seeds_default_on_first_open);
    RUN_TEST(test_profile_store_save_as_and_load_round_trip);
    RUN_TEST(test_profile_store_round_trips_keystroke_steps);
    RUN_TEST(test_profile_store_save_overwrites_active_profile);
    RUN_TEST(test_profile_store_rename);
    RUN_TEST(test_profile_store_delete_refuses_last_and_cascades);
    RUN_TEST(test_profile_store_delete_active_falls_back);
    RUN_TEST(test_profile_store_persists_and_reconnects_slot_assignment);

    if (g_failures == 0) {
        printf("\nAll tests passed.\n");
        return 0;
    }
    printf("\n%d assertion(s) failed.\n", g_failures);
    return 1;
}
