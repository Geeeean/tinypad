#pragma once

// System-wide keystroke synthesis, for macro buttons bound to a hotkey
// (e.g. Switch 1 -> Cmd+V). One implementation is compiled per OS:
//   - src/platform/keyboard/keyboard_macos.c   (CGEventPost)
//   - src/platform/keyboard/keyboard_windows.c (SendInput)
//   - src/platform/keyboard/keyboard_linux.c   (XTest -- X11/XWayland only,
//     see README for the native-Wayland caveat)
// All three expose the same keyboard_inject_shortcut(), so device_link.c
// never needs #ifdef.

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MACRO_MOD_CTRL = 1 << 0,
    MACRO_MOD_ALT = 1 << 1,
    MACRO_MOD_SHIFT = 1 << 2,
    MACRO_MOD_META = 1 << 3, // Cmd (macOS) / Win (Windows) / Super (Linux)
} macro_modifier_t;

// Deliberately a small, portable subset -- enough for typical macro-pad
// bindings, not a full keyboard layout. A-Z/0-9 are alphabetic, not
// physical-position based (no layout/locale handling).
typedef enum {
    MACRO_KEY_NONE = 0,
    MACRO_KEY_A, MACRO_KEY_B, MACRO_KEY_C, MACRO_KEY_D, MACRO_KEY_E, MACRO_KEY_F,
    MACRO_KEY_G, MACRO_KEY_H, MACRO_KEY_I, MACRO_KEY_J, MACRO_KEY_K, MACRO_KEY_L,
    MACRO_KEY_M, MACRO_KEY_N, MACRO_KEY_O, MACRO_KEY_P, MACRO_KEY_Q, MACRO_KEY_R,
    MACRO_KEY_S, MACRO_KEY_T, MACRO_KEY_U, MACRO_KEY_V, MACRO_KEY_W, MACRO_KEY_X,
    MACRO_KEY_Y, MACRO_KEY_Z,
    MACRO_KEY_0, MACRO_KEY_1, MACRO_KEY_2, MACRO_KEY_3, MACRO_KEY_4,
    MACRO_KEY_5, MACRO_KEY_6, MACRO_KEY_7, MACRO_KEY_8, MACRO_KEY_9,
    MACRO_KEY_SPACE, MACRO_KEY_ENTER, MACRO_KEY_TAB, MACRO_KEY_ESCAPE,
    MACRO_KEY_BACKSPACE, MACRO_KEY_DELETE,
    MACRO_KEY_UP, MACRO_KEY_DOWN, MACRO_KEY_LEFT, MACRO_KEY_RIGHT,
    MACRO_KEY_F1, MACRO_KEY_F2, MACRO_KEY_F3, MACRO_KEY_F4, MACRO_KEY_F5, MACRO_KEY_F6,
    MACRO_KEY_F7, MACRO_KEY_F8, MACRO_KEY_F9, MACRO_KEY_F10, MACRO_KEY_F11, MACRO_KEY_F12,
    MACRO_KEY_COUNT,
} macro_key_t;

// Synthesizes a system-wide key-down+key-up for `key` with `modifiers` held
// (bitmask of macro_modifier_t) -- e.g. MACRO_MOD_META|MACRO_MOD_... for
// Cmd+V. Returns false if the keystroke could not be sent: unknown/NONE
// key, no display connection (Linux), or missing OS permission (macOS
// Accessibility access -- see README). Implementations log the specific
// reason to stderr.
bool keyboard_inject_shortcut(uint32_t modifiers, macro_key_t key);
