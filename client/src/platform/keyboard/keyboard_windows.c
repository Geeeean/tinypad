// Windows keystroke injection via SendInput. Not build-verified on this
// (non-Windows) development machine.

#include "platform/keyboard_inject.h"
#include <stdio.h>
#include <windows.h>

static WORD vk_for(macro_key_t key)
{
    // VK codes for letters/digits equal their ASCII value on Windows.
    if (key >= MACRO_KEY_A && key <= MACRO_KEY_Z) return (WORD)('A' + (key - MACRO_KEY_A));
    if (key >= MACRO_KEY_0 && key <= MACRO_KEY_9) return (WORD)('0' + (key - MACRO_KEY_0));

    switch (key) {
    case MACRO_KEY_SPACE: return VK_SPACE;
    case MACRO_KEY_ENTER: return VK_RETURN;
    case MACRO_KEY_TAB: return VK_TAB;
    case MACRO_KEY_ESCAPE: return VK_ESCAPE;
    case MACRO_KEY_BACKSPACE: return VK_BACK;
    case MACRO_KEY_DELETE: return VK_DELETE;
    case MACRO_KEY_UP: return VK_UP;
    case MACRO_KEY_DOWN: return VK_DOWN;
    case MACRO_KEY_LEFT: return VK_LEFT;
    case MACRO_KEY_RIGHT: return VK_RIGHT;
    case MACRO_KEY_F1: return VK_F1; case MACRO_KEY_F2: return VK_F2;
    case MACRO_KEY_F3: return VK_F3; case MACRO_KEY_F4: return VK_F4;
    case MACRO_KEY_F5: return VK_F5; case MACRO_KEY_F6: return VK_F6;
    case MACRO_KEY_F7: return VK_F7; case MACRO_KEY_F8: return VK_F8;
    case MACRO_KEY_F9: return VK_F9; case MACRO_KEY_F10: return VK_F10;
    case MACRO_KEY_F11: return VK_F11; case MACRO_KEY_F12: return VK_F12;
    default: return 0;
    }
}

// The keyboard layout of whichever window is actually in the foreground --
// i.e. whatever the injected keystroke is about to land in -- not our own
// process's layout. VkKeyScanW() (no explicit HKL) only ever asks the
// *calling thread's* layout, which is tinypad's own and can differ from the
// target app's (different apps can have different active input languages).
// Falls back to our own thread's layout if there's no foreground window or
// its thread can't be queried.
static HKL foreground_layout(void)
{
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD tid = GetWindowThreadProcessId(fg, NULL);
        if (tid) {
            return GetKeyboardLayout(tid);
        }
    }
    return GetKeyboardLayout(0);
}

// The literal character each punctuation macro_key_t represents. Resolved to
// an actual VK via VkKeyScanExW() below instead of hardcoded VK_OEM_*
// constants -- those are documented by Microsoft as US-layout-specific (e.g.
// VK_OEM_2 is "the '/?' key" only on a US keyboard); on another layout the
// same VK code can be a completely different key/character, which is
// exactly what was producing the wrong symbol. VkKeyScanExW asks a specific
// layout which VK (and required shift state) actually produces this
// character -- the same idea keyboard_linux.c already uses via
// XKeysymToKeycode, just needing an explicit layout handle on Windows since
// the plain (non-Ex) VkKeyScanW only ever asks the calling thread's own.
static wchar_t char_for_punctuation(macro_key_t key)
{
    switch (key) {
    case MACRO_KEY_PERIOD: return L'.';
    case MACRO_KEY_COMMA: return L',';
    case MACRO_KEY_SLASH: return L'/';
    case MACRO_KEY_SEMICOLON: return L';';
    case MACRO_KEY_QUOTE: return L'\'';
    case MACRO_KEY_MINUS: return L'-';
    case MACRO_KEY_EQUAL: return L'=';
    case MACRO_KEY_LBRACKET: return L'[';
    case MACRO_KEY_RBRACKET: return L']';
    case MACRO_KEY_BACKSLASH: return L'\\';
    case MACRO_KEY_GRAVE: return L'`';
    default: return 0;
    }
}

// Resolves `key` to a VK code plus whether the current layout requires an
// implicit Shift to actually produce it (folded into *modifiers so it's
// pressed/released along with the caller's own modifiers). Returns false for
// an unsupported key or a punctuation character the current layout can't
// produce at all.
static bool resolve_key(macro_key_t key, uint32_t *modifiers, WORD *out_vk)
{
    wchar_t ch = char_for_punctuation(key);
    if (ch != 0) {
        SHORT scan = VkKeyScanExW(ch, foreground_layout());
        if (scan == -1) {
            return false;
        }
        *out_vk = LOBYTE(scan);
        if (HIBYTE(scan) & 1) {
            *modifiers |= MACRO_MOD_SHIFT;
        }
        return true;
    }

    WORD vk = vk_for(key);
    if (vk == 0) {
        return false;
    }
    *out_vk = vk;
    return true;
}

static INPUT make_key_input(WORD vk, bool key_up)
{
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0;
    return input;
}

// Injects a literal Unicode character, bypassing virtual-key/layout
// resolution entirely -- unlike the VK-based path, this cannot be
// layout-wrong, because there is no VK-to-character translation step for
// Windows to get wrong on any given layout. Only usable for a standalone
// character though: it carries no real VK (wVk=0), and many apps match
// keyboard shortcuts via VK+modifier accelerator tables that a
// KEYEVENTF_UNICODE event won't trigger -- so this is only used below when
// there are no modifiers to combine with.
static INPUT make_unicode_input(wchar_t ch, bool key_up)
{
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.wScan = (WORD)ch;
    input.ki.dwFlags = KEYEVENTF_UNICODE | (key_up ? KEYEVENTF_KEYUP : 0);
    return input;
}

bool keyboard_inject_shortcut(uint32_t modifiers, macro_key_t key)
{
    // Up to 4 modifiers + the key, pressed then released: 2*(4+1) = 10 max
    // (also enough for the 2-event bare-Unicode-character path below).
    INPUT inputs[10];
    int n = 0;

    wchar_t ch = char_for_punctuation(key);
    if (ch != 0 && modifiers == 0) {
        inputs[n++] = make_unicode_input(ch, false);
        inputs[n++] = make_unicode_input(ch, true);
    } else {
        WORD vk;
        if (!resolve_key(key, &modifiers, &vk)) {
            fprintf(stderr, "keyboard: unsupported key %d on the current keyboard layout\n",
                    (int)key);
            return false;
        }

        WORD mod_vks[4];
        int mod_count = 0;
        if (modifiers & MACRO_MOD_CTRL) mod_vks[mod_count++] = VK_CONTROL;
        if (modifiers & MACRO_MOD_ALT) mod_vks[mod_count++] = VK_MENU;
        if (modifiers & MACRO_MOD_SHIFT) mod_vks[mod_count++] = VK_SHIFT;
        if (modifiers & MACRO_MOD_META) mod_vks[mod_count++] = VK_LWIN;

        for (int i = 0; i < mod_count; i++) inputs[n++] = make_key_input(mod_vks[i], false);
        inputs[n++] = make_key_input(vk, false);
        inputs[n++] = make_key_input(vk, true);
        for (int i = mod_count - 1; i >= 0; i--) inputs[n++] = make_key_input(mod_vks[i], true);
    }

    UINT sent = SendInput((UINT)n, inputs, sizeof(INPUT));
    if (sent != (UINT)n) {
        fprintf(stderr, "keyboard: SendInput only accepted %u/%d events (error 0x%lX)\n", sent, n,
                GetLastError());
        return false;
    }
    return true;
}
