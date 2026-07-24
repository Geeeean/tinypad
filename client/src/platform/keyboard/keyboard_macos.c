// macOS keystroke injection via CGEventPost (Quartz Event Services).
//
// Requires the app to be granted Accessibility access (System Settings ->
// Privacy & Security -> Accessibility). AXIsProcessTrustedWithOptions with
// the prompt option triggers the system's "add this app" dialog the first
// time a keystroke is attempted; until the user grants it, every call
// fails and logs why.

#include "platform/keyboard_inject.h"
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdio.h>

// Physical-position keycodes for keys with no associated character --
// their position is what matters, not a layout-dependent glyph, so unlike
// the printable keys below these don't need live layout resolution.
static CGKeyCode keycode_for(macro_key_t key)
{
    switch (key) {
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

// The literal character each printable macro_key_t represents. Resolved to
// an actual CGKeyCode via char_keycode_lookup() below against the live
// keyboard layout, rather than hardcoded ANSI-US physical-position codes --
// on a non-US layout, a fixed physical position can be a totally different
// character (e.g. what's "-" on US ANSI can be "/" elsewhere), which is
// exactly the bug this replaces. Same idea as keyboard_windows.c's
// char_for_punctuation()/VkKeyScanExW and keyboard_linux.c's
// XKeysymToKeycode -- both already resolve against the live layout instead
// of a fixed table.
static char ascii_for_printable(macro_key_t key)
{
    if (key >= MACRO_KEY_A && key <= MACRO_KEY_Z) return (char)('a' + (key - MACRO_KEY_A));
    if (key >= MACRO_KEY_0 && key <= MACRO_KEY_9) return (char)('0' + (key - MACRO_KEY_0));

    switch (key) {
    case MACRO_KEY_PERIOD: return '.';
    case MACRO_KEY_COMMA: return ',';
    case MACRO_KEY_SLASH: return '/';
    case MACRO_KEY_SEMICOLON: return ';';
    case MACRO_KEY_QUOTE: return '\'';
    case MACRO_KEY_MINUS: return '-';
    case MACRO_KEY_EQUAL: return '=';
    case MACRO_KEY_LBRACKET: return '[';
    case MACRO_KEY_RBRACKET: return ']';
    case MACRO_KEY_BACKSLASH: return '\\';
    case MACRO_KEY_GRAVE: return '`';
    default: return 0;
    }
}

// Finds which physical key (CGKeyCode), under the live keyboard layout,
// produces `target`, and whether Shift is required to reach it. Mirrors
// keyboard_windows.c's VkKeyScanExW-based resolve_key(): only Shift is
// tried, not Option/AltGr-style combos, so a character that requires Option
// on some layout won't resolve -- the same gap Windows already has.
//
// Must run on the main thread: the TIS calls below live in HIToolbox, which
// asserts (SIGTRAP) if invoked off it. keyboard_inject_shortcut() itself is
// called from device_link.c's background polling thread, not main -- see
// char_keycode_lookup()'s dispatch_sync wrapper below.
static bool char_keycode_lookup_on_main_thread(UniChar target, CGKeyCode *out_code, bool *out_shift)
{
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    CFDataRef layout_data =
        source ? (CFDataRef)TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData) : NULL;

    // A legacy KCHR-only input source has no Unicode layout data; fall back
    // to the ASCII-capable layout, which Apple guarantees has it.
    if (!layout_data) {
        if (source) CFRelease(source);
        source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
        layout_data =
            source ? (CFDataRef)TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData) : NULL;
    }

    if (!layout_data) {
        if (source) CFRelease(source);
        return false;
    }

    const UCKeyboardLayout *layout = (const UCKeyboardLayout *)CFDataGetBytePtr(layout_data);
    bool found = false;

    // shift=false across the whole range first, then shift=true -- checking
    // shift first could report "Shift needed" for a key also reachable
    // without it.
    for (int shift = 0; !found && shift < 2; shift++) {
        for (CGKeyCode code = 0; code < 128; code++) {
            UInt32 dead_key_state = 0;
            UniCharCount length = 0;
            UniChar chars[4];
            UInt32 modifier_key_state = shift ? (UInt32)(shiftKey >> 8) : 0;
            OSStatus status =
                UCKeyTranslate(layout, code, kUCKeyActionDown, modifier_key_state, LMGetKbdType(),
                                kUCKeyTranslateNoDeadKeysBit, &dead_key_state,
                                sizeof(chars) / sizeof(chars[0]), &length, chars);
            if (status == noErr && length == 1 && chars[0] == target) {
                *out_code = code;
                *out_shift = shift != 0;
                found = true;
                break;
            }
        }
    }

    CFRelease(source);
    return found;
}

// Hops to the main thread if we're not already on it (the app's main thread
// always has a Cocoa run loop spinning -- either webview's own or tray_macos.m's
// [NSApp run] -- so this never stalls waiting for one to start; see main.c).
// pthread_main_np() avoids a redundant hop -- and a same-thread dispatch_sync
// deadlock -- when a macro happens to fire from the main thread itself.
static bool char_keycode_lookup(UniChar target, CGKeyCode *out_code, bool *out_shift)
{
    if (pthread_main_np()) {
        return char_keycode_lookup_on_main_thread(target, out_code, out_shift);
    }

    __block bool found = false;
    __block CGKeyCode code = 0;
    __block bool shift = false;
    dispatch_sync(dispatch_get_main_queue(), ^{
        found = char_keycode_lookup_on_main_thread(target, &code, &shift);
    });
    *out_code = code;
    *out_shift = shift;
    return found;
}

static bool resolve_key(macro_key_t key, uint32_t *modifiers, CGKeyCode *out_code)
{
    char ascii = ascii_for_printable(key);
    if (ascii != 0) {
        bool shift = false;
        if (!char_keycode_lookup((UniChar)ascii, out_code, &shift)) {
            return false;
        }
        if (shift) *modifiers |= MACRO_MOD_SHIFT;
        return true;
    }

    CGKeyCode code = keycode_for(key);
    if (code == 0xFFFF) {
        return false;
    }
    *out_code = code;
    return true;
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
    CGKeyCode code;
    if (!resolve_key(key, &modifiers, &code)) {
        fprintf(stderr, "keyboard: unsupported key %d on the current keyboard layout\n", (int)key);
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
