#pragma once
#include <stdint.h>
#include <cstddef>

#define PROTOCOL_START_BYTE 0xA5 // sync byte
#define MIXER_CHANNELS 5

struct __attribute__((packed)) mixer_data_in {
    uint8_t header;
    uint8_t volumes[MIXER_CHANNELS]; // (0-100)
    uint8_t valid;
    uint8_t checksum;
};

class Protocol {
  public:
    static uint8_t calculate_checksum(const uint8_t *volumes, size_t size);
    static bool parse_packet(const uint8_t *raw_buffer, size_t buffer_size,
                             mixer_data_in *out_packet);
};
