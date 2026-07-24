// Converts a live keydown into a KeystrokeStep, for "press the keys" macro
// recording (as opposed to keystrokeParser.ts's "type the sequence as
// text"). Both feed the same KeystrokeStep[] shape, so either editing mode
// can commit to the same macro.

import { MacroModifier, type KeystrokeStep } from "@/types";

// Letters and named/control keys have no layout-dependent character
// ambiguity under Shift (letters just change case, which folds back to the
// same base letter below; named keys aren't characters at all), so these
// are always resolved from event.key, never event.code -- see
// resolveMacroKey() for why event.key is the right source in general.
const NAMED_KEY_TO_MACRO_KEY: Record<string, number> = {
  Enter: 38, // also event.key for NumpadEnter -- no separate case needed
  Tab: 39,
  Escape: 40,
  Backspace: 41,
  Delete: 42,
  ArrowUp: 43,
  ArrowDown: 44,
  ArrowLeft: 45,
  ArrowRight: 46,
  F1: 47, F2: 48, F3: 49, F4: 50, F5: 51, F6: 52,
  F7: 53, F8: 54, F9: 55, F10: 56, F11: 57, F12: 58,
};

const PUNCTUATION_TO_MACRO_KEY: Record<string, number> = {
  ".": 59,
  ",": 60,
  "/": 61,
  ";": 62,
  "'": 63,
  "-": 64,
  "=": 65,
  "[": 66,
  "]": 67,
  "\\": 68,
  "`": 69,
};

// keyboard_inject.h's macro_key_t has no separate entry for a punctuation
// key's shifted glyph (e.g. no MACRO_KEY_PLUS) -- keystrokeParser.ts's Type
// mode already reflects this, spelling "+" as "shift+=" rather than as its
// own token. So a shifted glyph here resolves to its base key with Shift
// forced on (see keyEventToStep below), regardless of whether the browser
// actually reported Shift held: some layouts produce e.g. "+" from a
// dedicated, unshifted key, and native injection only knows how to reach
// "=" -- Shift is what turns that into "+" at playback time (see
// keyboard_macos.c's char_keycode_lookup()/resolve_key(), which folds a
// required Shift in the same way for the base character).
const SHIFTED_PUNCTUATION_ALIASES: Record<string, string> = {
  ">": ".",
  "<": ",",
  "?": "/",
  ":": ";",
  '"': "'",
  _: "-",
  "+": "=",
  "{": "[",
  "}": "]",
  "|": "\\",
  "~": "`",
};

// Digit row only, keyed by event.code -- used as the Shift-held fallback
// below, since e.g. Shift+1 -> "!" on a US layout: event.key is then a
// symbol we don't recognize as any digit, silently dropping common
// Cmd/Ctrl+Shift+<digit> shortcuts (tab switching, screenshot combos, ...)
// that don't actually care what glyph Shift produces, just which digit was
// held. event.code's physical position reliably still means the same digit
// across layouts for this narrow case.
const CODE_TO_DIGIT_KEY: Record<string, number> = (() => {
  const map: Record<string, number> = {};
  for (let i = 0; i <= 9; i++) map[`Digit${i}`] = 27 + i; // Digit0-Digit9 -> 27..36
  return map;
})();

// A modifier key pressed on its own never completes a step -- same as the
// text grammar, which has no bare-modifier token either. event.key doesn't
// distinguish Left/Right variants, unlike event.code, but that distinction
// isn't needed just to detect "this is a bare modifier."
const MODIFIER_KEYS = new Set(["Control", "Alt", "Shift", "Meta"]);

interface CapturableKeyEvent {
  key: string;
  code: string;
  ctrlKey: boolean;
  altKey: boolean;
  shiftKey: boolean;
  metaKey: boolean;
}

// event.key (the character/name the active OS keyboard layout actually
// produces) is the primary source, not event.code (the key's physical
// position on a reference ANSI layout). macro_key_t is a character
// identity for letters/punctuation -- native keystroke injection resolves
// e.g. MACRO_KEY_MINUS to "-" live against whatever layout is active (see
// keyboard_macos.c's char_keycode_lookup(), keyboard_windows.c's
// VkKeyScanExW, keyboard_linux.c's XKeysymToKeycode) -- so recording must
// key off the character too, or a physical key can be recorded as the
// wrong letter/symbol on a non-US layout (e.g. a "-" keycap sitting where
// "/" would be on a US keyboard misreports as "Slash" via event.code).
// Only bare digits fall back to event.code, and only when Shift is held;
// see CODE_TO_DIGIT_KEY above for why.
function resolveMacroKey(e: CapturableKeyEvent): number | undefined {
  if (e.key.length === 1) {
    const lower = e.key.toLowerCase();
    if (lower >= "a" && lower <= "z") {
      return 1 + (lower.charCodeAt(0) - "a".charCodeAt(0)); // MACRO_KEY_A..Z
    }
  }
  if (e.key in NAMED_KEY_TO_MACRO_KEY) {
    return NAMED_KEY_TO_MACRO_KEY[e.key];
  }
  if (e.key === " ") {
    return 37; // MACRO_KEY_SPACE
  }
  if (e.key in PUNCTUATION_TO_MACRO_KEY) {
    return PUNCTUATION_TO_MACRO_KEY[e.key];
  }
  const shiftedBase = SHIFTED_PUNCTUATION_ALIASES[e.key];
  if (shiftedBase !== undefined) {
    return PUNCTUATION_TO_MACRO_KEY[shiftedBase];
  }

  if (e.shiftKey && e.key.length === 1) {
    return CODE_TO_DIGIT_KEY[e.code];
  }
  if (e.key.length === 1 && e.key >= "0" && e.key <= "9") {
    return 27 + (e.key.charCodeAt(0) - "0".charCodeAt(0)); // MACRO_KEY_0..9
  }
  return undefined;
}

// Returns the step this keydown represents, or null if it doesn't complete
// one (a bare modifier, or a key this app has no macro representation for,
// e.g. CapsLock/PrintScreen). Caller is responsible for ignoring
// auto-repeat (KeyboardEvent.repeat) before calling this.
export function keyEventToStep(e: CapturableKeyEvent): KeystrokeStep | null {
  if (MODIFIER_KEYS.has(e.key)) {
    return null;
  }
  const key = resolveMacroKey(e);
  if (key === undefined) {
    return null;
  }

  let modifiers = 0;
  if (e.ctrlKey) modifiers |= MacroModifier.Ctrl;
  if (e.altKey) modifiers |= MacroModifier.Alt;
  if (e.shiftKey || e.key in SHIFTED_PUNCTUATION_ALIASES) modifiers |= MacroModifier.Shift;
  if (e.metaKey) modifiers |= MacroModifier.Meta;

  return { modifiers, key };
}
