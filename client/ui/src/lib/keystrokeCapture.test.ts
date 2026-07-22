import { describe, expect, it } from "vitest";
import { MacroModifier } from "@/types";
import { keyEventToStep } from "./keystrokeCapture";

function keyEvent(
  code: string,
  modifiers: Partial<Record<"ctrlKey" | "altKey" | "shiftKey" | "metaKey", boolean>> = {},
) {
  return {
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
    expect(keyEventToStep(keyEvent("KeyG"))).toEqual({ modifiers: 0, key: 7 }); // 'g' -> 1 + 6
  });

  it("captures a modifier held with a key", () => {
    expect(keyEventToStep(keyEvent("KeyV", { metaKey: true }))).toEqual({
      modifiers: MacroModifier.Meta,
      key: 22, // 'v' -> 1 + 21
    });
  });

  it("combines multiple held modifiers", () => {
    expect(keyEventToStep(keyEvent("KeyV", { metaKey: true, shiftKey: true }))).toEqual({
      modifiers: MacroModifier.Meta | MacroModifier.Shift,
      key: 22,
    });
  });

  it("captures digits", () => {
    expect(keyEventToStep(keyEvent("Digit0"))).toEqual({ modifiers: 0, key: 27 });
    expect(keyEventToStep(keyEvent("Digit9"))).toEqual({ modifiers: 0, key: 36 });
  });

  it("captures named/function keys", () => {
    expect(keyEventToStep(keyEvent("Escape"))).toEqual({ modifiers: 0, key: 40 });
    expect(keyEventToStep(keyEvent("Backspace"))).toEqual({ modifiers: 0, key: 41 });
    expect(keyEventToStep(keyEvent("F12"))).toEqual({ modifiers: 0, key: 58 });
    expect(keyEventToStep(keyEvent("NumpadEnter"))).toEqual(keyEventToStep(keyEvent("Enter")));
  });

  it("captures punctuation, matching keystrokeParser's index space", () => {
    expect(keyEventToStep(keyEvent("Period"))).toEqual({ modifiers: 0, key: 59 });
    expect(keyEventToStep(keyEvent("Slash", { metaKey: true }))).toEqual({
      modifiers: MacroModifier.Meta,
      key: 61,
    });
    expect(keyEventToStep(keyEvent("Backquote"))).toEqual({ modifiers: 0, key: 69 });
  });

  it("returns null for a bare modifier key press", () => {
    expect(keyEventToStep(keyEvent("MetaLeft", { metaKey: true }))).toBeNull();
    expect(keyEventToStep(keyEvent("ShiftRight", { shiftKey: true }))).toBeNull();
  });

  it("returns null for a physical key with no macro representation", () => {
    expect(keyEventToStep(keyEvent("CapsLock"))).toBeNull();
    expect(keyEventToStep(keyEvent("PrintScreen"))).toBeNull();
  });
});
