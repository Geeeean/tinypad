#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class Display {
public:
    static void init();
    static lgfx::LGFX_Device& get_device();

    // Blocks until the panel's next TE pulse (or a 60Hz-worth timeout if the
    // panel isn't signaling), so callers can push tear-free during vblank.
    static void wait_for_vsync();
};
