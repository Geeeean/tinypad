#include "pipewire_interface.h"
#ifdef _WIN32

#elif defined(__linux__)

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/iter.h>

#endif

#include <pthread.h>

int main(int argc, char *argv[])
{
#ifdef _WIN32
#elif defined(__linux__)
    pipewire_interface_init(argc, argv);
#else
    // todo log error
#endif

    return 0;
}
