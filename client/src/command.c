#include "command.h"
#include "state.h"
#include <zmq.h>

#define ENDPOINT "inproc://velvet"

ZmqConnection init_zmq_thread(void *ctx, int socket_type, const char *endpoint, bool bind)
{
    ZmqConnection conn = {.socket = NULL, .is_active = false};

    // Create socket for this specific thread
    conn.socket = zmq_socket(ctx, socket_type);
    if (!conn.socket)
        return conn;

    int rc;
    if (bind) {
        rc = zmq_bind(conn.socket, endpoint);
    } else {
        rc = zmq_connect(conn.socket, endpoint);
    }

    if (rc == 0)
        conn.is_active = true;

    return conn;
}

ZmqConnection command_connection_init_gui()
{
    return init_zmq_thread(shared_state.zmq_ctx, ZMQ_PAIR, ENDPOINT, true);
}

ZmqConnection command_connection_init_audio()
{
    return init_zmq_thread(shared_state.zmq_ctx, ZMQ_PAIR, ENDPOINT, false);
}

int command_send(ZmqConnection conn, Command cmd)
{
    if (!conn.socket || !conn.is_active)
        return -1;

    cmd.magic = VELVET_MAGIC;

    return zmq_send(conn.socket, &cmd, sizeof(Command), ZMQ_DONTWAIT);
}

int command_recv_blocking(ZmqConnection conn, Command *out_cmd)
{
    if (!conn.socket || !conn.is_active)
        return -1;

    return zmq_recv(conn.socket, out_cmd, sizeof(Command), 0);
}
