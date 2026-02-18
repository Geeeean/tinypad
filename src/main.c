#include "pipewire_interface.h"
#include <stdio.h>
#ifdef _WIN32

#elif defined(__linux__)

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/pod/iter.h>

#endif

#include "gui.h"
#include <pthread.h>

int main(int argc, char *argv[])
{
#ifdef _WIN32
#elif defined(__linux__)
    pipewire_loop_spawn();
#else
    // todo log error
#endif

    gui_init();

    return 0;
}
