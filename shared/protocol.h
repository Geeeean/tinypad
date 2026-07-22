#pragma once

// Wire protocol shared verbatim between the firmware (device) and the client
// (host); the single source of truth for packet layout. Logic lives in protocol.c.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIXER_CHANNELS 4
#define PROTOCOL_START_BYTE 0xA5 // sync byte
#define CHANNEL_NAME_LEN 16      // fixed, null-padded channel name length
#define MACRO_BUTTON_COUNT 8 // the 8 switches, each with an on-screen label box
#define MACRO_LABEL_LEN 10   // fixed, null-padded macro button label length

enum {
  PROTOCOL_PACKET_LEVELS =
      0x01, // host -> device, frequent: per-channel volume/left/right
  PROTOCOL_PACKET_METADATA =
      0x02, // host -> device, rare: channel names, sent on connect / on change
  PROTOCOL_PACKET_COMMAND_EVENT =
      0x03, // device -> host: a single button/encoder event
  PROTOCOL_PACKET_DEVICE_CONFIG =
      0x04, // host -> device, rare: macro labels + display settings
};

// One entry per physical input event. Values are explicit and stable, do not
// reorder
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

  // Fired instead of the plain PLUS/MINUS above when the encoder's button is
  // held down while it's turned -- a fine-grained (1%, vs. the normal 5%)
  // volume step.
  PROTOCOL_CMD_ENCODER_1_PLUS_FINE = 0x1C,
  PROTOCOL_CMD_ENCODER_1_MINUS_FINE = 0x1D,
  PROTOCOL_CMD_ENCODER_2_PLUS_FINE = 0x1E,
  PROTOCOL_CMD_ENCODER_2_MINUS_FINE = 0x1F,
  PROTOCOL_CMD_ENCODER_3_PLUS_FINE = 0x20,
  PROTOCOL_CMD_ENCODER_3_MINUS_FINE = 0x21,
  PROTOCOL_CMD_ENCODER_4_PLUS_FINE = 0x22,
  PROTOCOL_CMD_ENCODER_4_MINUS_FINE = 0x23,
};

// GUI dashboard pieces the device draws. device_config_packet.gui_layout is
// an ordered list of these ids (top-to-bottom); GUI_COMPONENT_NONE disables a slot.
enum {
  GUI_COMPONENT_VU_METERS = 0,
  GUI_COMPONENT_WAVEFORM = 1,
  GUI_COMPONENT_MACRO_GRID = 2,

  GUI_COMPONENT_COUNT
};
#define GUI_COMPONENT_NONE 0xFF // sentinel: this slot is disabled

#if defined(__GNUC__) || defined(__clang__)
#define PROTOCOL_PACKED __attribute__((packed))
#else
#define PROTOCOL_PACKED
#endif
#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif

// Common envelope for every packet: sync byte, type, and checksum, followed
// by a type-specific body. Purely a DRY grouping -- byte offsets are unchanged.
typedef struct PROTOCOL_PACKED {
  uint8_t header;   // PROTOCOL_START_BYTE
  uint8_t type;     // one of PROTOCOL_PACKET_*
  uint8_t checksum; // XOR of the body bytes that follow this envelope
} protocol_header_t;

typedef struct PROTOCOL_PACKED {
  uint8_t volume; // 0-100, mixer volume/gain setting
  uint8_t left;   // 0-100, left peak meter level
  uint8_t right;  // 0-100, right peak meter level
  uint8_t muted;  // 0/1, non-zero if the assigned session is muted
} channel_level;

typedef struct PROTOCOL_PACKED {
  protocol_header_t hdr; // type = PROTOCOL_PACKET_LEVELS
  channel_level channels[MIXER_CHANNELS];
} levels_packet;

typedef struct PROTOCOL_PACKED {
  protocol_header_t hdr; // type = PROTOCOL_PACKET_METADATA
  char names[MIXER_CHANNELS][CHANNEL_NAME_LEN];
} metadata_packet;

typedef struct PROTOCOL_PACKED {
  protocol_header_t hdr; // type = PROTOCOL_PACKET_COMMAND_EVENT
  uint8_t command;       // one of the PROTOCOL_CMD_* values
} command_event_packet;

typedef struct PROTOCOL_PACKED {
  protocol_header_t hdr; // type = PROTOCOL_PACKET_DEVICE_CONFIG
  char macro_labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN];
  uint8_t gui_layout[GUI_COMPONENT_COUNT]; // draw order, top-to-bottom;
                                           // GUI_COMPONENT_NONE disables
                                           // that slot
} device_config_packet;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

// protocol_header_t must be exactly 3 packed bytes -- everything else
// assumes byte offsets 0/1/2 for header/type/checksum.
#ifdef __cplusplus
static_assert(sizeof(protocol_header_t) == 3,
              "protocol_header_t must not be padded");
#else
_Static_assert(sizeof(protocol_header_t) == 3,
               "protocol_header_t must not be padded");
#endif

// Largest packet on the wire; sized for RX buffers that must hold any packet
// type. Computed from the structs above, so it stays correct as they change.
#define PROTOCOL_MAX2(a, b) ((a) > (b) ? (a) : (b))
#define PROTOCOL_MAX_PACKET_SIZE                                               \
  PROTOCOL_MAX2(PROTOCOL_MAX2(sizeof(levels_packet), sizeof(metadata_packet)), \
                PROTOCOL_MAX2(sizeof(command_event_packet),                    \
                              sizeof(device_config_packet)))

// --- Public API, implemented in protocol.c ------------------------------

uint8_t protocol_calculate_checksum(const uint8_t *data, size_t size);

// Resolves the total wire size of a packet from its type byte. Returns false
// for an unrecognized type.
bool protocol_size_for_type(uint8_t type, size_t *out_size);

// Validates raw_buffer as a packet of expected_type and copies it into
// out_packet on success. Pass out_size via the PROTOCOL_PARSE macro below.
bool protocol_parse_packet(uint8_t expected_type, const uint8_t *raw_buffer,
                           size_t raw_size, void *out_packet, size_t out_size);
#define PROTOCOL_PARSE(type_const, buf, size, out)                             \
  protocol_parse_packet((type_const), (buf), (size), (out), sizeof(*(out)))

void protocol_build_levels_packet(levels_packet *out_packet,
                                  const channel_level channels[MIXER_CHANNELS]);
void protocol_build_metadata_packet(
    metadata_packet *out_packet,
    const char names[MIXER_CHANNELS][CHANNEL_NAME_LEN]);
void protocol_build_command_event_packet(uint8_t command,
                                         command_event_packet *out_packet);
void protocol_build_device_config_packet(
    device_config_packet *out_packet,
    const char macro_labels[MACRO_BUTTON_COUNT][MACRO_LABEL_LEN],
    const uint8_t gui_layout[GUI_COMPONENT_COUNT]);

// Clears any out-of-range or duplicate id to GUI_COMPONENT_NONE in place.
// Already applied when building a packet; a raw parsed one still needs it.
void protocol_normalize_gui_layout(uint8_t gui_layout[GUI_COMPONENT_COUNT]);

// --- Resumable byte-stream framing --------------------------------------
// Shared by both sides' serial receive loop: feed it one byte at a time and
// it reassembles a packet across calls, resyncing after any framing error.

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
  PROTOCOL_FEED_COMPLETE,     // reader->buffer[0..size) holds a valid packet of
                              // *out_type
  PROTOCOL_FEED_BAD_TYPE,     // type byte unrecognized; reader resynced on SYNC
  PROTOCOL_FEED_BAD_CHECKSUM, // full packet arrived but checksum mismatched;
                              // resynced
} protocol_feed_result_t;

void protocol_reader_init(protocol_reader_t *reader);

protocol_feed_result_t protocol_reader_feed(protocol_reader_t *reader,
                                            uint8_t byte, uint8_t *out_type,
                                            size_t *out_size);

#ifdef __cplusplus
} // extern "C"
#endif
