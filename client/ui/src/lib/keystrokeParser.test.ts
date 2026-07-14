import { describe, expect, it } from "vitest";
import { MacroModifier } from "@/types";
import {
  parseKeystrokeSequence,
  parseKeystrokeToken,
  stringifyKeystrokeSequence,
} from "./keystrokeParser";

describe("parseKeystrokeToken", () => {
  it("parses a bare key", () => {
    expect(parseKeystrokeToken("g")).toEqual({ modifiers: 0, key: 7 }); // 'g' -> 1 + 6
  });

  it("parses a single modifier + key", () => {
    expect(parseKeystrokeToken("cmd+v")).toEqual({
      modifiers: MacroModifier.Meta,
      key: 22, // 'v' -> 1 + 21
    });
  });

  it("combines multiple modifiers, order-independent", () => {
    const a = parseKeystrokeToken("cmd+shift+v");
    const b = parseKeystrokeToken("shift+cmd+v");
    expect(a).toEqual({ modifiers: MacroModifier.Meta | MacroModifier.Shift, key: 22 });
    expect(a).toEqual(b);
  });

  it("is case-insensitive", () => {
    expect(parseKeystrokeToken("CMD+TAB")).toEqual(parseKeystrokeToken("cmd+tab"));
  });

  it("accepts modifier aliases (ctrl/control, alt/option/opt, win/windows/super/meta)", () => {
    expect(parseKeystrokeToken("control+a")?.modifiers).toBe(MacroModifier.Ctrl);
    expect(parseKeystrokeToken("option+a")?.modifiers).toBe(MacroModifier.Alt);
    expect(parseKeystrokeToken("opt+a")?.modifiers).toBe(MacroModifier.Alt);
    expect(parseKeystrokeToken("windows+a")?.modifiers).toBe(MacroModifier.Meta);
    expect(parseKeystrokeToken("super+a")?.modifiers).toBe(MacroModifier.Meta);
  });

  it("accepts named and function keys", () => {
    expect(parseKeystrokeToken("enter")).not.toBeNull();
    expect(parseKeystrokeToken("return")).toEqual(parseKeystrokeToken("enter"));
    expect(parseKeystrokeToken("esc")).toEqual(parseKeystrokeToken("escape"));
    expect(parseKeystrokeToken("bksp")).toEqual(parseKeystrokeToken("backspace"));
    expect(parseKeystrokeToken("del")).toEqual(parseKeystrokeToken("delete"));
    expect(parseKeystrokeToken("f12")).not.toBeNull();
  });

  it("accepts digits", () => {
    expect(parseKeystrokeToken("5")).not.toBeNull();
  });

  it("rejects an unrecognized key", () => {
    expect(parseKeystrokeToken("nonsense")).toBeNull();
  });

  it("rejects an unrecognized modifier", () => {
    expect(parseKeystrokeToken("foo+v")).toBeNull();
  });

  it("rejects an empty token", () => {
    expect(parseKeystrokeToken("")).toBeNull();
    expect(parseKeystrokeToken("+")).toBeNull();
  });

  it("rejects a modifier with no key", () => {
    expect(parseKeystrokeToken("cmd+")).toBeNull();
  });
});

describe("parseKeystrokeSequence", () => {
  it("parses a whitespace-separated sequence into steps", () => {
    const { steps, errors } = parseKeystrokeSequence("g e a n cmd+tab");
    expect(errors).toEqual([]);
    expect(steps).toHaveLength(5);
    expect(steps[4]).toEqual({ modifiers: MacroModifier.Meta, key: 39 }); // tab -> 39
  });

  it("tolerates extra whitespace", () => {
    const { steps } = parseKeystrokeSequence("  g   e  ");
    expect(steps).toHaveLength(2);
  });

  it("returns an empty sequence for blank input", () => {
    expect(parseKeystrokeSequence("")).toEqual({ steps: [], errors: [] });
    expect(parseKeystrokeSequence("   ")).toEqual({ steps: [], errors: [] });
  });

  it("collects one error per unparseable token and still parses the rest", () => {
    const { steps, errors } = parseKeystrokeSequence("g bogus cmd+v also-bogus");
    expect(steps).toHaveLength(2);
    expect(errors).toHaveLength(2);
    expect(errors[0]).toContain("bogus");
    expect(errors[1]).toContain("also-bogus");
  });
});

describe("stringifyKeystrokeSequence", () => {
  it("round-trips through parseKeystrokeSequence", () => {
    const original = "g e a n cmd+shift+tab";
    const { steps } = parseKeystrokeSequence(original);
    const text = stringifyKeystrokeSequence(steps);
    const reparsed = parseKeystrokeSequence(text);
    expect(reparsed.steps).toEqual(steps);
  });

  it("orders modifiers as ctrl, alt, shift, cmd regardless of input order", () => {
    const { steps } = parseKeystrokeSequence("shift+cmd+alt+ctrl+v");
    expect(stringifyKeystrokeSequence(steps)).toBe("ctrl+alt+shift+cmd+v");
  });

  it("produces an empty string for an empty step list", () => {
    expect(stringifyKeystrokeSequence([])).toBe("");
  });
});
