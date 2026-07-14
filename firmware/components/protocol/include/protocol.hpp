#pragma once

// C++ ergonomics on top of shared/protocol.h, which is the actual wire
// contract (packet layout, checksum, framing) and is shared verbatim with
// the client. This header only adds strongly-typed enums and a thin
// Protocol wrapper so call sites elsewhere in the firmware keep their
// existing Protocol::foo(...) / Command::FOO shape.
#include "protocol.h"
#include <cstdint>

enum class PacketType : uint8_t {
    LEVELS = PROTOCOL_PACKET_LEVELS,
    METADATA = PROTOCOL_PACKET_METADATA,
    COMMAND_EVENT = PROTOCOL_PACKET_COMMAND_EVENT,
    DEVICE_CONFIG = PROTOCOL_PACKET_DEVICE_CONFIG,
};

// Mirrors the PROTOCOL_CMD_* values in shared/protocol.h -- that header is
// the source of truth for the numeric values, this just gives them a type.
enum class Command : uint8_t {
    SWITCH_1 = PROTOCOL_CMD_SWITCH_1,
    SWITCH_2 = PROTOCOL_CMD_SWITCH_2,
    SWITCH_3 = PROTOCOL_CMD_SWITCH_3,
    SWITCH_4 = PROTOCOL_CMD_SWITCH_4,
    SWITCH_5 = PROTOCOL_CMD_SWITCH_5,
    SWITCH_6 = PROTOCOL_CMD_SWITCH_6,
    SWITCH_7 = PROTOCOL_CMD_SWITCH_7,
    SWITCH_8 = PROTOCOL_CMD_SWITCH_8,

    ENCODER_1_PLUS = PROTOCOL_CMD_ENCODER_1_PLUS,
    ENCODER_1_MINUS = PROTOCOL_CMD_ENCODER_1_MINUS,
    ENCODER_1_BTN = PROTOCOL_CMD_ENCODER_1_BTN,
    ENCODER_2_PLUS = PROTOCOL_CMD_ENCODER_2_PLUS,
    ENCODER_2_MINUS = PROTOCOL_CMD_ENCODER_2_MINUS,
    ENCODER_2_BTN = PROTOCOL_CMD_ENCODER_2_BTN,
    ENCODER_3_PLUS = PROTOCOL_CMD_ENCODER_3_PLUS,
    ENCODER_3_MINUS = PROTOCOL_CMD_ENCODER_3_MINUS,
    ENCODER_3_BTN = PROTOCOL_CMD_ENCODER_3_BTN,
    ENCODER_4_PLUS = PROTOCOL_CMD_ENCODER_4_PLUS,
    ENCODER_4_MINUS = PROTOCOL_CMD_ENCODER_4_MINUS,
    ENCODER_4_BTN = PROTOCOL_CMD_ENCODER_4_BTN,
};

class Protocol {
  public:
    static uint8_t calculate_checksum(const uint8_t *data, size_t size)
    {
        return protocol_calculate_checksum(data, size);
    }

    // Resolves the total wire size of a packet from its type byte.
    // Returns false for an unrecognized type.
    static bool size_for_type(uint8_t type, size_t *out_size)
    {
        return protocol_size_for_type(type, out_size);
    }

    static bool parse_levels_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                    levels_packet *out_packet)
    {
        return protocol_parse_levels_packet(raw_buffer, buffer_size, out_packet);
    }

    static bool parse_metadata_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                      metadata_packet *out_packet)
    {
        return protocol_parse_metadata_packet(raw_buffer, buffer_size, out_packet);
    }

    static bool parse_device_config_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                           device_config_packet *out_packet)
    {
        return protocol_parse_device_config_packet(raw_buffer, buffer_size, out_packet);
    }

    // Fills header/type/command/checksum for a device -> host command event.
    static void build_command_event_packet(Command command, command_event_packet *out_packet)
    {
        protocol_build_command_event_packet(static_cast<uint8_t>(command), out_packet);
    }
};
