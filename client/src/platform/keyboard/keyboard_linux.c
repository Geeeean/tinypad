// Linux keystroke injection via the X11 XTest extension. Works under X11
// and XWayland; a native (non-XWayland) Wayland session has no equivalent
// standard API and will fail to open a display -- see README. A uinput-based
// backend would work under both but needs elevated/udev-granted permission
// to /dev/uinput, so it's left as a follow-up rather than the default.

#include "platform/keyboard_inject.h"
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <stdio.h>

static KeySym keysym_for(macro_key_t key)
{
    // X11 keysyms for a-z/0-9 equal their ASCII value.
    if (key >= MACRO_KEY_A && key <= MACRO_KEY_Z) return XK_a + (key - MACRO_KEY_A);
    if (key >= MACRO_KEY_0 && key <= MACRO_KEY_9) return XK_0 + (key - MACRO_KEY_0);

    switch (key) {
    case MACRO_KEY_SPACE: return XK_space;
    case MACRO_KEY_ENTER: return XK_Return;
    case MACRO_KEY_TAB: return XK_Tab;
    case MACRO_KEY_ESCAPE: return XK_Escape;
    case MACRO_KEY_BACKSPACE: return XK_BackSpace;
    case MACRO_KEY_DELETE: return XK_Delete;
    case MACRO_KEY_UP: return XK_Up;
    case MACRO_KEY_DOWN: return XK_Down;
    case MACRO_KEY_LEFT: return XK_Left;
    case MACRO_KEY_RIGHT: return XK_Right;
    case MACRO_KEY_F1: return XK_F1; case MACRO_KEY_F2: return XK_F2;
    case MACRO_KEY_F3: return XK_F3; case MACRO_KEY_F4: return XK_F4;
    case MACRO_KEY_F5: return XK_F5; case MACRO_KEY_F6: return XK_F6;
    case MACRO_KEY_F7: return XK_F7; case MACRO_KEY_F8: return XK_F8;
    case MACRO_KEY_F9: return XK_F9; case MACRO_KEY_F10: return XK_F10;
    case MACRO_KEY_F11: return XK_F11; case MACRO_KEY_F12: return XK_F12;
    case MACRO_KEY_PERIOD: return XK_period;
    case MACRO_KEY_COMMA: return XK_comma;
    case MACRO_KEY_SLASH: return XK_slash;
    case MACRO_KEY_SEMICOLON: return XK_semicolon;
    case MACRO_KEY_QUOTE: return XK_apostrophe;
    case MACRO_KEY_MINUS: return XK_minus;
    case MACRO_KEY_EQUAL: return XK_equal;
    case MACRO_KEY_LBRACKET: return XK_bracketleft;
    case MACRO_KEY_RBRACKET: return XK_bracketright;
    case MACRO_KEY_BACKSLASH: return XK_backslash;
    case MACRO_KEY_GRAVE: return XK_grave;
    default: return NoSymbol;
    }
}

bool keyboard_inject_shortcut(uint32_t modifiers, macro_key_t key)
{
    KeySym sym = keysym_for(key);
    if (sym == NoSymbol) {
        fprintf(stderr, "keyboard: unsupported key %d\n", (int)key);
        return false;
    }

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "keyboard: XOpenDisplay failed -- no X11 display (native Wayland "
                        "sessions aren't supported yet, see README)\n");
        return false;
    }

    KeyCode keycode = XKeysymToKeycode(display, sym);
    if (keycode == 0) {
        fprintf(stderr, "keyboard: no keycode for the requested key on this keyboard layout\n");
        XCloseDisplay(display);
        return false;
    }

    KeySym mod_syms[4];
    int mod_count = 0;
    if (modifiers & MACRO_MOD_CTRL) mod_syms[mod_count++] = XK_Control_L;
    if (modifiers & MACRO_MOD_ALT) mod_syms[mod_count++] = XK_Alt_L;
    if (modifiers & MACRO_MOD_SHIFT) mod_syms[mod_count++] = XK_Shift_L;
    if (modifiers & MACRO_MOD_META) mod_syms[mod_count++] = XK_Super_L;

    KeyCode mod_codes[4];
    for (int i = 0; i < mod_count; i++) {
        mod_codes[i] = XKeysymToKeycode(display, mod_syms[i]);
        XTestFakeKeyEvent(display, mod_codes[i], True, 0);
    }

    XTestFakeKeyEvent(display, keycode, True, 0);
    XTestFakeKeyEvent(display, keycode, False, 0);

    for (int i = mod_count - 1; i >= 0; i--) {
        XTestFakeKeyEvent(display, mod_codes[i], False, 0);
    }

    XFlush(display);
    XCloseDisplay(display);
    return true;
}
