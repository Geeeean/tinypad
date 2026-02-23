#pragma once

#include <stdint.h>

#define MAX_CHANNELS 3
#define MAX_NAME_LENGTH 16

typedef struct {
  uint8_t id;
  char name[MAX_NAME_LENGTH];
  float volume; // Volume 0.0 - 1.0
  float peak;   // Peak 0.0 - 1.0
  uint8_t knob_index;
} MixerChannel;

typedef enum {
  EVENT_VOLUME_CHANGED,
  EVENT_PEAK_UPDATED,
  EVENT_NAME_CHANGED,
  EVENT_MUTE_SET
} MixerEventType;

typedef struct {
  MixerEventType type;
  uint8_t channel_id;
  union {
    float volume;
    int peak;
    char name[16];
  } payload;
} MixerEvent;

typedef struct {
  MixerChannel channels[MAX_CHANNELS];
  uint8_t active_channels;
} MixerState;
