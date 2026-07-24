import { describe, expect, it } from "vitest";
import { MacroModifier } from "@/types";
import { keyEventToStep } from "./keystrokeCapture";

function keyEvent(
  key: string,
  code: string,
  modifiers: Partial<Record<"ctrlKey" | "altKey" | "shiftKey" | "metaKey", boolean>> = {},
) {
  return {
    key,
    code,
    ctrlKey: false,
    altKey: false,
    shiftKey: false,
    metaKey: false,
    ...modifiers,
  };
}

describe("keyEventToStep", () => {
  it("captures a bare letter key", () => {
    expect(keyEventToStep(keyEvent("g", "KeyG"))).toEqual({ modifiers: 0, key: 7 }); // 'g' -> 1 + 6
  });

  it("captures an uppercased letter (Shift held) as the same base letter", () => {
    expect(keyEventToStep(keyEvent("G", "KeyG", { shiftKey: true }))).toEqual({
      modifiers: MacroModifier.Shift,
      key: 7,
    });
  });

  it("captures a modifier held with a key", () => {
    expect(keyEventToStep(keyEvent("v", "KeyV", { metaKey: true }))).toEqual({
      modifiers: MacroModifier.Meta,
      key: 22, // 'v' -> 1 + 21
    });
  });

  it("combines multiple held modifiers", () => {
    expect(keyEventToStep(keyEvent("v", "KeyV", { metaKey: true, shiftKey: true }))).toEqual({
      modifiers: MacroModifier.Meta | MacroModifier.Shift,
      key: 22,
    });
  });

  it("captures digits", () => {
    expect(keyEventToStep(keyEvent("0", "Digit0"))).toEqual({ modifiers: 0, key: 27 });
    expect(keyEventToStep(keyEvent("9", "Digit9"))).toEqual({ modifiers: 0, key: 36 });
  });

  it("captures a digit combined with Shift positionally, even though Shift changes the typed character", () => {
    // Shift+1 types "!" on a US layout -- the recorder should still capture
    // "digit 1 + Shift" (e.g. for a Cmd+Shift+1 shortcut), not drop it.
    expect(keyEventToStep(keyEvent("!", "Digit1", { metaKey: true, shiftKey: true }))).toEqual({
      modifiers: MacroModifier.Meta | MacroModifier.Shift,
      key: 28, // MACRO_KEY_1
    });
  });

  it("captures named/function keys", () => {
    expect(keyEventToStep(keyEvent("Escape", "Escape"))).toEqual({ modifiers: 0, key: 40 });
    expect(keyEventToStep(keyEvent("Backspace", "Backspace"))).toEqual({ modifiers: 0, key: 41 });
    expect(keyEventToStep(keyEvent("F12", "F12"))).toEqual({ modifiers: 0, key: 58 });
    expect(keyEventToStep(keyEvent("Enter", "NumpadEnter"))).toEqual(
      keyEventToStep(keyEvent("Enter", "Enter")),
    );
  });

  it("captures punctuation by the character actually produced, matching keystrokeParser's index space", () => {
    expect(keyEventToStep(keyEvent(".", "Period"))).toEqual({ modifiers: 0, key: 59 });
    expect(keyEventToStep(keyEvent("/", "Slash", { metaKey: true }))).toEqual({
      modifiers: MacroModifier.Meta,
      key: 61,
    });
    expect(keyEventToStep(keyEvent("`", "Backquote"))).toEqual({ modifiers: 0, key: 69 });
  });

  it("captures punctuation by character even when event.code doesn't match a US layout's position", () => {
    // On a layout where the "-" keycap physically sits where "/" would be
    // on a US keyboard, event.code reports the US-reference position
    // ("Slash") but event.key correctly reports the character produced
    // ("-"). The recorder must trust event.key here, or it records the
    // wrong macro key entirely (this was the reported bug).
    expect(keyEventToStep(keyEvent("-", "Slash"))).toEqual({ modifiers: 0, key: 64 }); // MACRO_KEY_MINUS
  });

  it("resolves a shifted punctuation glyph to its base key with Shift forced on", () => {
    // Shift+Minus types "_" on a US layout, not one of the supported
    // punctuation characters (macro_key_t has no MACRO_KEY_UNDERSCORE) --
    // resolve it to the base "-" key with Shift forced on, same as
    // keystrokeParser.ts's Type mode spells "_" as "shift+-".
    expect(keyEventToStep(keyEvent("_", "Minus", { metaKey: true, shiftKey: true }))).toEqual({
      modifiers: MacroModifier.Meta | MacroModifier.Shift,
      key: 64, // MACRO_KEY_MINUS
    });
  });

  it("resolves a shifted punctuation glyph even when the browser reports no Shift held", () => {
    // Some layouts produce "+" from a dedicated, unshifted key rather than
    // Shift+Equal (e.g. a physical Italian keyboard) -- macro_key_t only
    // models "=", so this must still resolve to MACRO_KEY_EQUAL with Shift
    // forced on regardless of e.shiftKey, or native injection (which only
    // knows how to reach "=") would play back "=" instead of "+".
    expect(keyEventToStep(keyEvent("+", "BracketRight", { metaKey: true }))).toEqual({
      modifiers: MacroModifier.Meta | MacroModifier.Shift,
      key: 65, // MACRO_KEY_EQUAL
    });
  });

  it("returns null for a bare modifier key press", () => {
    expect(keyEventToStep(keyEvent("Meta", "MetaLeft", { metaKey: true }))).toBeNull();
    expect(keyEventToStep(keyEvent("Shift", "ShiftRight", { shiftKey: true }))).toBeNull();
  });

  it("returns null for a key with no macro representation", () => {
    expect(keyEventToStep(keyEvent("CapsLock", "CapsLock"))).toBeNull();
    expect(keyEventToStep(keyEvent("PrintScreen", "PrintScreen"))).toBeNull();
  });
});
