#include "common.h"
#include "pipewire_interface.h"
#include "state.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
    if (state_init()) {
        exit(FAILURE);
    }

#ifdef _WIN32
#elif defined(__linux__)
    pipewire_loop_spawn();
#else
    // todo log error
#endif

    gui_init();

    return SUCCESS;
}
