#pragma once

// Wire protocol shared verbatim between the firmware (device) and the client
// (host). This is the single source of truth for packet layout, so it is a
// plain C header consumable from both the ESP-IDF C++ build and the client's
// C build -- do not fork a second copy of these definitions.
//
// Byte order: little-endian for anything wider than a byte (none of the
// current packets have multi-byte numeric fields, so this is currently moot,
// but keep it in mind if one is added).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTOCOL_START_BYTE 0xA5 // sync byte
#define MIXER_CHANNELS 4
#define CHANNEL_NAME_LEN 16 // fixed, null-padded channel name length
#define MACRO_BUTTON_COUNT 8 // the 8 switches, each with an on-screen label box
#define MACRO_LABEL_LEN 10   // fixed, null-padded macro button label length

enum {
    PROTOCOL_PACKET_LEVELS = 0x01,        // host -> device, frequent: per-channel volume/left/right
    PROTOCOL_PACKET_METADATA = 0x02,      // host -> device, rare: channel names, sent on connect / on change
    PROTOCOL_PACKET_COMMAND_EVENT = 0x03, // device -> host: a single button/encoder event
    PROTOCOL_PACKET_DEVICE_CONFIG = 0x04, // host -> device, rare: macro labels + display settings
};

// One entry per physical input event. Values are explicit and stable: this
// byte is a wire contract with whatever's listening on the host, so members
// must never be reordered/renumbered once assigned.
enum {
    PROTOCOL_CMD_SWITCH_1 = 0x00,
    PROTOCOL_CMD_SWITCH_2 = 0x01,
    PROTOCOL_CMD_SWITCH_3 = 0x02,
    PROTOCOL_CMD_SWITCH_4 = 0x03,
    PROTOCOL_CMD_SWITCH_5 = 0x04,
    PROTOCOL_CMD_SWITCH_6 = 0x05,
    PROTOCOL_CMD_SWITCH_7 = 0x06,
    PROTOCOL_CMD_SWITCH_8 = 0x07,

    PROTOCOL_CMD_ENCODER_1_PLUS = 0x10,
    PROTOCOL_CMD_ENCODER_1_MINUS = 0x11,
    PROTOCOL_CMD_ENCODER_1_BTN = 0x12,
    PROTOCOL_CMD_ENCODER_2_PLUS = 0x13,
    PROTOCOL_CMD_ENCODER_2_MINUS = 0x14,
    PROTOCOL_CMD_ENCODER_2_BTN = 0x15,
    PROTOCOL_CMD_ENCODER_3_PLUS = 0x16,
    PROTOCOL_CMD_ENCODER_3_MINUS = 0x17,
    PROTOCOL_CMD_ENCODER_3_BTN = 0x18,
    PROTOCOL_CMD_ENCODER_4_PLUS = 0x19,
    PROTOCOL_CMD_ENCODER_4_MINUS = 0x1A,
    PROTOCOL_CMD_ENCODER_4_BTN = 0x1B,
};

#if defined(__GNUC__) || defined(__clang__)
#define PROTOCOL_PACKED __attribute__((packed))
#else
#define PROTOCOL_PACKED
#endif
#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif

typedef struct PROTOCOL_PACKED {
    uint8_t volume; // 0-100, mixer volume/gain setting
    uint8_t left;   // 0-100, left peak meter level
    uint8_t right;  // 0-100, right peak meter level
} channel_level;

typedef struct PROTOCOL_PACKED {
    uint8_t header; // PROTOCOL_START_BYTE
    uint8_t type;   // PROTOCOL_PACKET_LEVELS
    channel_level channels[MIXER_CHANNELS];
    uint8_t valid; // wakes the display when non-zero
    uint8_t checksum;
} levels_packet;

typedef struct PROTOCOL_PACKED {
    uint8_t header; // PROTOCOL_START_BYTE
    uint8_t type;   // PROTOCOL_PACKET_METADATA
    char names[MIXER_CHANNELS][CHANNEL_NAME_LEN];
    uint8_t checksum;
} metadata_packet;

typedef struct PROTOCOL_PACKED {
    uint8_t header;  // PROTOCOL_START_BYTE
    uint8_t type;    // PROTOCOL_PACKET_COMMAND_EVENT
    uint8_t command; // one of the PROTOCOL_CMD_* values
    uint8_t checksum;
} command_event_packet;

typedef struct PROTOCOL_PACKED {
    uint8_t header; // PROTOCOL_START_BYTE
    uint8_t type;   // PROTOCOL_PACKET_DEVICE_CONFIG
    char macro_labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN];
    uint8_t show_graph; // non-zero: draw the output waveform graph
    uint8_t checksum;
} device_config_packet;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

// Largest packet on the wire; sized for RX buffers that must hold any packet
// type. Both structs are already defined above, so this is a compile-time
// constant expression, not a guess to keep in sync by hand.
#define PROTOCOL_MAX_PACKET_SIZE \
    (sizeof(device_config_packet) > sizeof(metadata_packet) ? sizeof(device_config_packet) \
                                                             : sizeof(metadata_packet))

static inline uint8_t protocol_calculate_checksum(const uint8_t *data, size_t size)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

// Resolves the total wire size of a packet from its type byte. Returns false
// for an unrecognized type.
static inline bool protocol_size_for_type(uint8_t type, size_t *out_size)
{
    switch (type) {
    case PROTOCOL_PACKET_LEVELS:
        *out_size = sizeof(levels_packet);
        return true;
    case PROTOCOL_PACKET_METADATA:
        *out_size = sizeof(metadata_packet);
        return true;
    case PROTOCOL_PACKET_COMMAND_EVENT:
        *out_size = sizeof(command_event_packet);
        return true;
    case PROTOCOL_PACKET_DEVICE_CONFIG:
        *out_size = sizeof(device_config_packet);
        return true;
    default:
        return false;
    }
}

// Checksum covers everything between the header and the checksum byte
// (i.e. excludes both), matching protocol_finalize_packet below.
static inline bool protocol_validate_checksum(const uint8_t *raw_buffer, size_t size)
{
    uint8_t packet_checksum = raw_buffer[size - 1];
    uint8_t calculated_checksum = protocol_calculate_checksum(&raw_buffer[1], size - 2);
    return packet_checksum == calculated_checksum;
}

// Fills raw_buffer[size - 1] with the checksum of raw_buffer[1 .. size - 2].
// Call after header/type/body are populated but before sending.
static inline void protocol_finalize_packet(uint8_t *raw_buffer, size_t size)
{
    raw_buffer[size - 1] = protocol_calculate_checksum(&raw_buffer[1], size - 2);
}

static inline bool protocol_parse_levels_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                                 levels_packet *out_packet)
{
    if (buffer_size != sizeof(levels_packet) || raw_buffer[0] != PROTOCOL_START_BYTE ||
        !protocol_validate_checksum(raw_buffer, buffer_size)) {
        return false;
    }
    memcpy(out_packet, raw_buffer, sizeof(levels_packet));
    return true;
}

static inline bool protocol_parse_metadata_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                                    metadata_packet *out_packet)
{
    if (buffer_size != sizeof(metadata_packet) || raw_buffer[0] != PROTOCOL_START_BYTE ||
        !protocol_validate_checksum(raw_buffer, buffer_size)) {
        return false;
    }
    memcpy(out_packet, raw_buffer, sizeof(metadata_packet));
    return true;
}

static inline bool protocol_parse_command_event_packet(const uint8_t *raw_buffer,
                                                         size_t buffer_size,
                                                         command_event_packet *out_packet)
{
    if (buffer_size != sizeof(command_event_packet) || raw_buffer[0] != PROTOCOL_START_BYTE ||
        !protocol_validate_checksum(raw_buffer, buffer_size)) {
        return false;
    }
    memcpy(out_packet, raw_buffer, sizeof(command_event_packet));
    return true;
}

static inline bool protocol_parse_device_config_packet(const uint8_t *raw_buffer,
                                                         size_t buffer_size,
                                                         device_config_packet *out_packet)
{
    if (buffer_size != sizeof(device_config_packet) || raw_buffer[0] != PROTOCOL_START_BYTE ||
        !protocol_validate_checksum(raw_buffer, buffer_size)) {
        return false;
    }
    memcpy(out_packet, raw_buffer, sizeof(device_config_packet));
    return true;
}

static inline void protocol_build_levels_packet(levels_packet *out_packet,
                                                  const channel_level channels[MIXER_CHANNELS],
                                                  uint8_t valid)
{
    out_packet->header = PROTOCOL_START_BYTE;
    out_packet->type = PROTOCOL_PACKET_LEVELS;
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        out_packet->channels[i] = channels[i];
    }
    out_packet->valid = valid;
    protocol_finalize_packet((uint8_t *)out_packet, sizeof(*out_packet));
}

static inline void
protocol_build_metadata_packet(metadata_packet *out_packet,
                                const char names[MIXER_CHANNELS][CHANNEL_NAME_LEN])
{
    out_packet->header = PROTOCOL_START_BYTE;
    out_packet->type = PROTOCOL_PACKET_METADATA;
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        memcpy(out_packet->names[i], names[i], CHANNEL_NAME_LEN);
    }
    protocol_finalize_packet((uint8_t *)out_packet, sizeof(*out_packet));
}

static inline void protocol_build_command_event_packet(uint8_t command,
                                                         command_event_packet *out_packet)
{
    out_packet->header = PROTOCOL_START_BYTE;
    out_packet->type = PROTOCOL_PACKET_COMMAND_EVENT;
    out_packet->command = command;
    protocol_finalize_packet((uint8_t *)out_packet, sizeof(*out_packet));
}

static inline void protocol_build_device_config_packet(
    device_config_packet *out_packet, const char macro_labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN],
    uint8_t show_graph)
{
    out_packet->header = PROTOCOL_START_BYTE;
    out_packet->type = PROTOCOL_PACKET_DEVICE_CONFIG;
    for (int i = 0; i < MACRO_BUTTON_COUNT; i++) {
        memcpy(out_packet->macro_labels[i], macro_labels[i], MACRO_LABEL_LEN);
    }
    out_packet->show_graph = show_graph;
    protocol_finalize_packet((uint8_t *)out_packet, sizeof(*out_packet));
}

// --- Resumable byte-stream framing --------------------------------------
//
// Shared by both sides' serial receive loop: feed it one byte at a time as
// it arrives and it reassembles a full packet across calls, resyncing on
// the next PROTOCOL_START_BYTE after any framing error. Used by usb_manager
// (firmware, receiving LEVELS/METADATA) and device_link (client, receiving
// COMMAND_EVENT).
//
// This intentionally reads one byte at a time rather than slurping whatever
// is available in one call -- at these packet rates the extra call overhead
// is noise, and it's what lets both sides share this exact state machine
// instead of each hand-rolling their own.

typedef enum {
    PROTOCOL_RX_SYNC = 0,
    PROTOCOL_RX_TYPE,
    PROTOCOL_RX_BODY,
} protocol_rx_state_t;

typedef struct {
    protocol_rx_state_t state;
    uint8_t buffer[PROTOCOL_MAX_PACKET_SIZE];
    size_t filled;
    size_t target;
} protocol_reader_t;

typedef enum {
    PROTOCOL_FEED_INCOMPLETE = 0, // need more bytes
    PROTOCOL_FEED_COMPLETE,       // reader->buffer[0..size) holds a valid packet of *out_type
    PROTOCOL_FEED_BAD_TYPE,       // type byte unrecognized; reader resynced on SYNC
    PROTOCOL_FEED_BAD_CHECKSUM,   // full packet arrived but checksum mismatched; resynced
} protocol_feed_result_t;

static inline void protocol_reader_init(protocol_reader_t *reader)
{
    reader->state = PROTOCOL_RX_SYNC;
    reader->filled = 0;
    reader->target = 0;
}

static inline protocol_feed_result_t protocol_reader_feed(protocol_reader_t *reader,
                                                            uint8_t byte, uint8_t *out_type,
                                                            size_t *out_size)
{
    switch (reader->state) {
    case PROTOCOL_RX_SYNC:
        if (byte == PROTOCOL_START_BYTE) {
            reader->buffer[0] = byte;
            reader->filled = 1;
            reader->state = PROTOCOL_RX_TYPE;
        }
        return PROTOCOL_FEED_INCOMPLETE;

    case PROTOCOL_RX_TYPE:
        reader->buffer[1] = byte;
        reader->filled = 2;

        if (!protocol_size_for_type(byte, &reader->target)) {
            protocol_reader_init(reader);
            return PROTOCOL_FEED_BAD_TYPE;
        }

        reader->state = PROTOCOL_RX_BODY;
        return PROTOCOL_FEED_INCOMPLETE;

    case PROTOCOL_RX_BODY:
        reader->buffer[reader->filled++] = byte;
        if (reader->filled < reader->target) {
            return PROTOCOL_FEED_INCOMPLETE;
        }

        {
            uint8_t type = reader->buffer[1];
            size_t size = reader->target;
            bool ok = protocol_validate_checksum(reader->buffer, size);

            protocol_reader_init(reader);

            if (!ok) {
                return PROTOCOL_FEED_BAD_CHECKSUM;
            }

            *out_type = type;
            *out_size = size;
            return PROTOCOL_FEED_COMPLETE;
        }
    }

    return PROTOCOL_FEED_INCOMPLETE;
}

#ifdef __cplusplus
} // extern "C"
#endif
