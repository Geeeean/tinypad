// POSIX termios backend for serial_port.h, used on Linux and macOS (both
// expose the firmware's USB CDC-ACM endpoint as a /dev tty node -- the
// termios handling is identical, only the typical device path differs).

#include "platform/serial_port.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct serial_port {
    int fd;
};

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return B115200;
    }
}

serial_port_t *serial_port_open(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return NULL;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return NULL;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, baud_to_speed(SERIAL_BAUD_RATE));
    cfsetospeed(&tty, baud_to_speed(SERIAL_BAUD_RATE));

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    // Pure poll: read() always returns immediately with whatever is already
    // buffered (0 bytes is a valid, non-error result). Matches
    // serial_port_read()'s non-blocking contract.
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return NULL;
    }

    serial_port_t *port = malloc(sizeof(serial_port_t));
    if (!port) {
        close(fd);
        return NULL;
    }
    port->fd = fd;
    return port;
}

void serial_port_close(serial_port_t *port)
{
    if (!port) {
        return;
    }
    close(port->fd);
    free(port);
}

int serial_port_read(serial_port_t *port, uint8_t *buffer, size_t size)
{
    ssize_t n = read(port->fd, buffer, size);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    return (int)n;
}

bool serial_port_write(serial_port_t *port, const uint8_t *data, size_t size)
{
    size_t written = 0;
    int stalls = 0;

    while (written < size) {
        ssize_t n = write(port->fd, data + written, size - written);
        if (n < 0) {
            // The fd is O_NONBLOCK so a full USB write buffer shows up as
            // EAGAIN; wait briefly and retry rather than treating it as a
            // real error, but bail out (as a disconnect) if it never drains.
            if ((errno == EAGAIN || errno == EWOULDBLOCK) && stalls < 1000) {
                stalls++;
                struct timespec ts = {.tv_sec = 0, .tv_nsec = 200000}; // 0.2ms
                nanosleep(&ts, NULL);
                continue;
            }
            return false;
        }
        stalls = 0;
        written += (size_t)n;
    }
    return true;
}
