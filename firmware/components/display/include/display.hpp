#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class Display {
public:
    static void init();
    static lgfx::LGFX_Device& get_device();
};
