// macOS keystroke injection via CGEventPost (Quartz Event Services).
//
// Requires the app to be granted Accessibility access (System Settings ->
// Privacy & Security -> Accessibility). AXIsProcessTrustedWithOptions with
// the prompt option triggers the system's "add this app" dialog the first
// time a keystroke is attempted; until the user grants it, every call
// fails and logs why.

#include "platform/keyboard_inject.h"
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>

static CGKeyCode keycode_for(macro_key_t key)
{
    switch (key) {
    case MACRO_KEY_A: return 0x00; case MACRO_KEY_B: return 0x0B;
    case MACRO_KEY_C: return 0x08; case MACRO_KEY_D: return 0x02;
    case MACRO_KEY_E: return 0x0E; case MACRO_KEY_F: return 0x03;
    case MACRO_KEY_G: return 0x05; case MACRO_KEY_H: return 0x04;
    case MACRO_KEY_I: return 0x22; case MACRO_KEY_J: return 0x26;
    case MACRO_KEY_K: return 0x28; case MACRO_KEY_L: return 0x25;
    case MACRO_KEY_M: return 0x2E; case MACRO_KEY_N: return 0x2D;
    case MACRO_KEY_O: return 0x1F; case MACRO_KEY_P: return 0x23;
    case MACRO_KEY_Q: return 0x0C; case MACRO_KEY_R: return 0x0F;
    case MACRO_KEY_S: return 0x01; case MACRO_KEY_T: return 0x11;
    case MACRO_KEY_U: return 0x20; case MACRO_KEY_V: return 0x09;
    case MACRO_KEY_W: return 0x0D; case MACRO_KEY_X: return 0x07;
    case MACRO_KEY_Y: return 0x10; case MACRO_KEY_Z: return 0x06;
    case MACRO_KEY_0: return 0x1D; case MACRO_KEY_1: return 0x12;
    case MACRO_KEY_2: return 0x13; case MACRO_KEY_3: return 0x14;
    case MACRO_KEY_4: return 0x15; case MACRO_KEY_5: return 0x17;
    case MACRO_KEY_6: return 0x16; case MACRO_KEY_7: return 0x1A;
    case MACRO_KEY_8: return 0x1C; case MACRO_KEY_9: return 0x19;
    case MACRO_KEY_SPACE: return 0x31;
    case MACRO_KEY_ENTER: return 0x24;
    case MACRO_KEY_TAB: return 0x30;
    case MACRO_KEY_ESCAPE: return 0x35;
    case MACRO_KEY_BACKSPACE: return 0x33;
    case MACRO_KEY_DELETE: return 0x75;
    case MACRO_KEY_UP: return 0x7E;
    case MACRO_KEY_DOWN: return 0x7D;
    case MACRO_KEY_LEFT: return 0x7B;
    case MACRO_KEY_RIGHT: return 0x7C;
    case MACRO_KEY_F1: return 0x7A; case MACRO_KEY_F2: return 0x78;
    case MACRO_KEY_F3: return 0x63; case MACRO_KEY_F4: return 0x76;
    case MACRO_KEY_F5: return 0x60; case MACRO_KEY_F6: return 0x61;
    case MACRO_KEY_F7: return 0x62; case MACRO_KEY_F8: return 0x64;
    case MACRO_KEY_F9: return 0x65; case MACRO_KEY_F10: return 0x6D;
    case MACRO_KEY_F11: return 0x67; case MACRO_KEY_F12: return 0x6F;
    default: return 0xFFFF;
    }
}

static bool ensure_accessibility_permission(void)
{
    if (AXIsProcessTrusted()) {
        return true;
    }

    // Shows the system "grant Accessibility access" dialog (once the OS
    // decides to; it may not re-prompt on every call). The user still has
    // to go flip the switch in System Settings themselves -- this call
    // does not block waiting for that, so it still returns false here even
    // on the run where they've just granted it moments too late.
    CFStringRef keys[] = {kAXTrustedCheckOptionPrompt};
    CFBooleanRef values[] = {kCFBooleanTrue};
    CFDictionaryRef options = CFDictionaryCreate(
        NULL, (const void **)keys, (const void **)values, 1, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    bool trusted = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    return trusted;
}

bool keyboard_inject_shortcut(uint32_t modifiers, macro_key_t key)
{
    CGKeyCode code = keycode_for(key);
    if (code == 0xFFFF) {
        fprintf(stderr, "keyboard: unsupported key %d\n", (int)key);
        return false;
    }

    if (!ensure_accessibility_permission()) {
        fprintf(stderr, "keyboard: Accessibility access not granted -- enable TinyPad under "
                        "System Settings > Privacy & Security > Accessibility\n");
        return false;
    }

    CGEventFlags flags = 0;
    if (modifiers & MACRO_MOD_CTRL) flags |= kCGEventFlagMaskControl;
    if (modifiers & MACRO_MOD_ALT) flags |= kCGEventFlagMaskAlternate;
    if (modifiers & MACRO_MOD_SHIFT) flags |= kCGEventFlagMaskShift;
    if (modifiers & MACRO_MOD_META) flags |= kCGEventFlagMaskCommand;

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef key_down = CGEventCreateKeyboardEvent(source, code, true);
    CGEventRef key_up = CGEventCreateKeyboardEvent(source, code, false);
    CGEventSetFlags(key_down, flags);
    CGEventSetFlags(key_up, flags);

    CGEventPost(kCGHIDEventTap, key_down);
    CGEventPost(kCGHIDEventTap, key_up);

    CFRelease(key_down);
    CFRelease(key_up);
    CFRelease(source);
    return true;
}
