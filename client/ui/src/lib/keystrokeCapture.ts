// Converts a live keydown into a KeystrokeStep, for "press the keys" macro
// recording (as opposed to keystrokeParser.ts's "type the sequence as
// text"). Both feed the same KeystrokeStep[] shape, so either editing mode
// can commit to the same macro.

import { MacroModifier, type KeystrokeStep } from "@/types";

// event.code (physical key, independent of the active keyboard layout) ->
// index into MACRO_KEY_LABELS. Mirrors macro_key_t's order in
// keyboard_inject.h, same index space keystrokeParser.ts's KEY_ALIASES
// targets -- just keyed by DOM event code instead of typed text.
const CODE_TO_KEY: Record<string, number> = (() => {
  const map: Record<string, number> = {};
  for (let i = 0; i < 26; i++) map[`Key${String.fromCharCode(65 + i)}`] = 1 + i; // KeyA-KeyZ -> 1..26
  for (let i = 0; i <= 9; i++) map[`Digit${i}`] = 27 + i; // Digit0-Digit9 -> 27..36
  map.Space = 37;
  map.Enter = 38;
  map.NumpadEnter = 38;
  map.Tab = 39;
  map.Escape = 40;
  map.Backspace = 41;
  map.Delete = 42;
  map.ArrowUp = 43;
  map.ArrowDown = 44;
  map.ArrowLeft = 45;
  map.ArrowRight = 46;
  for (let i = 1; i <= 12; i++) map[`F${i}`] = 46 + i; // F1-F12 -> 47..58
  map.Period = 59;
  map.Comma = 60;
  map.Slash = 61;
  map.Semicolon = 62;
  map.Quote = 63;
  map.Minus = 64;
  map.Equal = 65;
  map.BracketLeft = 66;
  map.BracketRight = 67;
  map.Backslash = 68;
  map.Backquote = 69;
  return map;
})();

// A modifier key pressed on its own never completes a step -- same as the
// text grammar, which has no bare-modifier token either.
const MODIFIER_CODES = new Set([
  "ControlLeft",
  "ControlRight",
  "AltLeft",
  "AltRight",
  "ShiftLeft",
  "ShiftRight",
  "MetaLeft",
  "MetaRight",
]);

interface CapturableKeyEvent {
  code: string;
  ctrlKey: boolean;
  altKey: boolean;
  shiftKey: boolean;
  metaKey: boolean;
}

// Returns the step this keydown represents, or null if it doesn't complete
// one (a bare modifier, or a physical key this app has no macro
// representation for, e.g. CapsLock/PrintScreen). Caller is responsible for
// ignoring auto-repeat (KeyboardEvent.repeat) before calling this.
export function keyEventToStep(e: CapturableKeyEvent): KeystrokeStep | null {
  if (MODIFIER_CODES.has(e.code)) {
    return null;
  }
  const key = CODE_TO_KEY[e.code];
  if (key === undefined) {
    return null;
  }

  let modifiers = 0;
  if (e.ctrlKey) modifiers |= MacroModifier.Ctrl;
  if (e.altKey) modifiers |= MacroModifier.Alt;
  if (e.shiftKey) modifiers |= MacroModifier.Shift;
  if (e.metaKey) modifiers |= MacroModifier.Meta;

  return { modifiers, key };
}
