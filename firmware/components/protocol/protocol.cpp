#include "protocol.hpp"
#include "esp_log.h"
#include <cstring>

#define TAG "PROTOCOL"

uint8_t Protocol::calculate_checksum(const uint8_t *data, size_t size)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum ^= data[i];
    }

    return checksum;
}

bool Protocol::size_for_type(uint8_t type, size_t *out_size)
{
    switch (static_cast<PacketType>(type)) {
    case PacketType::LEVELS:
        *out_size = sizeof(levels_packet);
        return true;
    case PacketType::METADATA:
        *out_size = sizeof(metadata_packet);
        return true;
    case PacketType::COMMAND_EVENT:
        *out_size = sizeof(command_event_packet);
        return true;
    default:
        return false;
    }
}

bool Protocol::validate_checksum(const uint8_t *raw_buffer, size_t size)
{
    uint8_t packet_checksum = raw_buffer[size - 1];
    uint8_t calculated_checksum = calculate_checksum(&raw_buffer[1], size - 2);
    return packet_checksum == calculated_checksum;
}

bool Protocol::parse_levels_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                   levels_packet *out_packet)
{
    if (buffer_size != sizeof(levels_packet)) {
        ESP_LOGE(TAG, "levels packet: unexpected size %u", (unsigned)buffer_size);
        return false;
    }

    if (raw_buffer[0] != PROTOCOL_START_BYTE) {
        ESP_LOGE(TAG, "levels packet: bad start byte");
        return false;
    }

    if (!validate_checksum(raw_buffer, buffer_size)) {
        ESP_LOGE(TAG, "levels packet: checksum mismatch");
        return false;
    }

    std::memcpy(out_packet, raw_buffer, sizeof(levels_packet));
    return true;
}

bool Protocol::parse_metadata_packet(const uint8_t *raw_buffer, size_t buffer_size,
                                     metadata_packet *out_packet)
{
    if (buffer_size != sizeof(metadata_packet)) {
        ESP_LOGE(TAG, "metadata packet: unexpected size %u", (unsigned)buffer_size);
        return false;
    }

    if (raw_buffer[0] != PROTOCOL_START_BYTE) {
        ESP_LOGE(TAG, "metadata packet: bad start byte");
        return false;
    }

    if (!validate_checksum(raw_buffer, buffer_size)) {
        ESP_LOGE(TAG, "metadata packet: checksum mismatch");
        return false;
    }

    std::memcpy(out_packet, raw_buffer, sizeof(metadata_packet));
    return true;
}

void Protocol::build_command_event_packet(Command command, command_event_packet *out_packet)
{
    out_packet->header = PROTOCOL_START_BYTE;
    out_packet->type = static_cast<uint8_t>(PacketType::COMMAND_EVENT);
    out_packet->command = static_cast<uint8_t>(command);

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(out_packet);
    out_packet->checksum =
        calculate_checksum(&raw[1], sizeof(command_event_packet) - 2);
}
