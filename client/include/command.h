#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#define VELVET_MAGIC 0x564C5654 // "VLVT"

typedef enum { CMD_SET_VOLUME = 0, CMD_TOGGLE_MUTE, CMD_START, CMD_QUIT } CommandType;

typedef struct __attribute__((packed)) {
    uint32_t magic;   // Always set to VELVET_MAGIC
    CommandType type; // CommandType
    uint32_t node_id; // Target PipeWire node ID
    float value;      // Volume value (0.0f - 1.0f)
} Command;

typedef struct {
    void *socket;
    bool is_active;
} ZmqConnection;

ZmqConnection command_connection_init_gui();
ZmqConnection command_connection_init_audio();

int command_send(ZmqConnection conn, Command cmd);
int command_recv_blocking(ZmqConnection conn, Command *out_cmd);

#endif
