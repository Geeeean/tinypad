#include "protocol.h"

#include <string.h>

// Checksum covers the type-specific body only, i.e. everything after the
// 3-byte header/type/checksum envelope.
static bool protocol_validate_checksum(const uint8_t *raw_buffer, size_t size)
{
    uint8_t packet_checksum = raw_buffer[2];
    uint8_t calculated_checksum =
        protocol_calculate_checksum(&raw_buffer[3], size - 3);
    return packet_checksum == calculated_checksum;
}

// Fills raw_buffer[2] with the checksum of raw_buffer[3 .. size). Call after
// header/type/body are populated but before sending.
static void protocol_finalize_packet(uint8_t *raw_buffer, size_t size)
{
    raw_buffer[2] = protocol_calculate_checksum(&raw_buffer[3], size - 3);
}

uint8_t protocol_calculate_checksum(const uint8_t *data, size_t size)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

bool protocol_size_for_type(uint8_t type, size_t *out_size)
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

bool protocol_parse_packet(uint8_t expected_type, const uint8_t *raw_buffer,
                           size_t raw_size, void *out_packet, size_t out_size)
{
    size_t expected_size;
    if (!protocol_size_for_type(expected_type, &expected_size) ||
        out_size != expected_size || raw_size != expected_size ||
        raw_buffer[0] != PROTOCOL_START_BYTE || raw_buffer[1] != expected_type ||
        !protocol_validate_checksum(raw_buffer, raw_size)) {
        return false;
    }
    memcpy(out_packet, raw_buffer, expected_size);
    return true;
}

void protocol_build_levels_packet(levels_packet *out_packet,
                                  const channel_level channels[MIXER_CHANNELS])
{
    out_packet->hdr.header = PROTOCOL_START_BYTE;
    out_packet->hdr.type = PROTOCOL_PACKET_LEVELS;
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        out_packet->channels[i] = channels[i];
    }
    protocol_finalize_packet((uint8_t *)out_packet, sizeof(*out_packet));
}

void protocol_build_metadata_packet(
    metadata_packet *out_packet,
    const char names[MIXER_CHANNELS][CHANNEL_NAME_LEN])
{
    out_packet->hdr.header = PROTOCOL_START_BYTE;
    out_packet->hdr.type = PROTOCOL_PACKET_METADATA;
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        memcpy(out_packet->names[i], names[i], CHANNEL_NAME_LEN);
    }
    protocol_finalize_packet((uint8_t *)out_packet, sizeof(*out_packet));
}

void protocol_build_command_event_packet(uint8_t command,
                                         command_event_packet *out_packet)
{
    out_packet->hdr.header = PROTOCOL_START_BYTE;
    out_packet->hdr.type = PROTOCOL_PACKET_COMMAND_EVENT;
    out_packet->command = command;
    protocol_finalize_packet((uint8_t *)out_packet, sizeof(*out_packet));
}

void protocol_normalize_gui_layout(uint8_t gui_layout[GUI_COMPONENT_COUNT])
{
    bool seen[GUI_COMPONENT_COUNT] = {false};
    for (int i = 0; i < GUI_COMPONENT_COUNT; i++) {
        uint8_t id = gui_layout[i];
        if (id == GUI_COMPONENT_NONE) {
            continue;
        }
        if (id >= GUI_COMPONENT_COUNT || seen[id]) {
            gui_layout[i] = GUI_COMPONENT_NONE;
            continue;
        }
        seen[id] = true;
    }
}

void protocol_build_device_config_packet(
    device_config_packet *out_packet,
    const char macro_labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN],
    const uint8_t gui_layout[GUI_COMPONENT_COUNT])
{
    out_packet->hdr.header = PROTOCOL_START_BYTE;
    out_packet->hdr.type = PROTOCOL_PACKET_DEVICE_CONFIG;
    for (int i = 0; i < MACRO_BUTTON_COUNT; i++) {
        memcpy(out_packet->macro_labels[i], macro_labels[i], MACRO_LABEL_LEN);
    }
    memcpy(out_packet->gui_layout, gui_layout, sizeof(out_packet->gui_layout));
    protocol_normalize_gui_layout(out_packet->gui_layout);
    protocol_finalize_packet((uint8_t *)out_packet, sizeof(*out_packet));
}

void protocol_reader_init(protocol_reader_t *reader)
{
    reader->state = PROTOCOL_RX_SYNC;
    reader->filled = 0;
    reader->target = 0;
}

protocol_feed_result_t protocol_reader_feed(protocol_reader_t *reader,
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
