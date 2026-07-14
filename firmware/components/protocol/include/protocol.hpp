#pragma once
#include <stdint.h>
#include <cstddef>

#define PROTOCOL_START_BYTE 0xA5 // sync byte
#define MIXER_CHANNELS 4
#define CHANNEL_NAME_LEN 16 // fixed, null-padded channel name length

enum class PacketType : uint8_t {
    LEVELS = 0x01,        // host -> device, frequent: per-channel volume/left/right
    METADATA = 0x02,      // host -> device, rare: channel names, sent on connect / on change
    COMMAND_EVENT = 0x03, // device -> host: a single button/encoder event
};

// One entry per physical input event. Values are explicit and stable: this
// byte is a wire contract with whatever's listening on the host, so members
// must never be reordered/renumbered once assigned.
enum class Command : uint8_t {
    SWITCH_1 = 0x00,
    SWITCH_2 = 0x01,
    SWITCH_3 = 0x02,
    SWITCH_4 = 0x03,
    SWITCH_5 = 0x04,
    SWITCH_6 = 0x05,
    SWITCH_7 = 0x06,
    SWITCH_8 = 0x07,

    ENCODER_1_PLUS = 0x10,
    ENCODER_1_MINUS = 0x11,
    ENCODER_1_BTN = 0x12,
    ENCODER_2_PLUS = 0x13,
    ENCODER_2_MINUS = 0x14,
    ENCODER_2_BTN = 0x15,
    ENCODER_3_PLUS = 0x16,
    ENCODER_3_MINUS = 0x17,
    ENCODER_3_BTN = 0x18,
    ENCODER_4_PLUS = 0x19,
    ENCODER_4_MINUS = 0x1A,
    ENCODER_4_BTN = 0x1B,
};

struct __attribute__((packed)) channel_level {
    uint8_t volume; // 0-100, mixer volume/gain setting
    uint8_t left;   // 0-100, left peak meter level
    uint8_t right;  // 0-100, right peak meter level
};

struct __attribute__((packed)) levels_packet {
    uint8_t header; // PROTOCOL_START_BYTE
    uint8_t type;   // PacketType::LEVELS
    channel_level channels[MIXER_CHANNELS];
    uint8_t valid; // wakes the display when non-zero
    uint8_t checksum;
};

struct __attribute__((packed)) metadata_packet {
    uint8_t header; // PROTOCOL_START_BYTE
    uint8_t type;   // PacketType::METADATA
    char names[MIXER_CHANNELS][CHANNEL_NAME_LEN];
    uint8_t checksum;
};

struct __attribute__((packed)) command_event_packet {
    uint8_t header; // PROTOCOL_START_BYTE
    uint8_t type;   // PacketType::COMMAND_EVENT
    uint8_t command; // Command
    uint8_t checksum;
};

class Protocol {
  public:
    static uint8_t calculate_checksum(const uint8_t *data, size_t size);

    // Resolves the total wire size of a packet from its type byte.
    // Returns false for an unrecognized type.
    static bool size_for_type(uint8_t type, size_t *out_size);

    static bool parse_levels_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                    levels_packet *out_packet);
    static bool parse_metadata_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                      metadata_packet *out_packet);

    // Fills header/type/command/checksum for a device -> host command event.
    static void build_command_event_packet(Command command, command_event_packet *out_packet);

  private:
    static bool validate_checksum(const uint8_t *raw_buffer, size_t size);
};
