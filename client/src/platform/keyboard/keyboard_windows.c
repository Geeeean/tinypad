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

static INPUT make_key_input(WORD vk, bool key_up)
{
    INPUT input;
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0;
    return input;
}

bool keyboard_inject_shortcut(uint32_t modifiers, macro_key_t key)
{
    WORD vk = vk_for(key);
    if (vk == 0) {
        fprintf(stderr, "keyboard: unsupported key %d\n", (int)key);
        return false;
    }

    WORD mod_vks[4];
    int mod_count = 0;
    if (modifiers & MACRO_MOD_CTRL) mod_vks[mod_count++] = VK_CONTROL;
    if (modifiers & MACRO_MOD_ALT) mod_vks[mod_count++] = VK_MENU;
    if (modifiers & MACRO_MOD_SHIFT) mod_vks[mod_count++] = VK_SHIFT;
    if (modifiers & MACRO_MOD_META) mod_vks[mod_count++] = VK_LWIN;

    // Up to 4 modifiers + the key, pressed then released: 2*(4+1) = 10 max.
    INPUT inputs[10];
    int n = 0;
    for (int i = 0; i < mod_count; i++) inputs[n++] = make_key_input(mod_vks[i], false);
    inputs[n++] = make_key_input(vk, false);
    inputs[n++] = make_key_input(vk, true);
    for (int i = mod_count - 1; i >= 0; i--) inputs[n++] = make_key_input(mod_vks[i], true);

    UINT sent = SendInput((UINT)n, inputs, sizeof(INPUT));
    if (sent != (UINT)n) {
        fprintf(stderr, "keyboard: SendInput only accepted %u/%d events (error 0x%lX)\n", sent, n,
                GetLastError());
        return false;
    }
    return true;
}
