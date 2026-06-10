#include "protocol.hpp"
#include "esp_log.h"

#define TAG "PROTOCOL"

uint8_t Protocol::calculate_checksum(const uint8_t *volumes, size_t size)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum ^= volumes[i];
    }

    return checksum;
}

bool Protocol::parse_packet(const uint8_t *raw_buffer, size_t buffer_size,
                            mixer_data_in *out_packet)
{
    if (buffer_size < sizeof(mixer_data_in)) {
        ESP_LOGE(TAG, "buffer size < sizeof(mixer_data_in)");
        return false;
    }

    if (raw_buffer[0] != PROTOCOL_START_BYTE) {
        ESP_LOGE(TAG, "raw_buffer[0] != PROTOCOL_START_BYTE");
        return false;
    }

    uint8_t packet_checksum = raw_buffer[sizeof(mixer_data_in) - 1];
    uint8_t calculated_checksum = calculate_checksum(&raw_buffer[1], buffer_size - 2);

    if (packet_checksum != calculated_checksum) {
        ESP_LOGE(TAG, "packet_checksum != calculated_checksum");
        return false;
    }

    out_packet->header = raw_buffer[0];
    for (int i = 0; i < MIXER_CHANNELS; i++) {
        out_packet->volumes[i] = raw_buffer[1 + i];
    }
    out_packet->valid = raw_buffer[1 + MIXER_CHANNELS];
    out_packet->checksum = packet_checksum;

    return true;
}
