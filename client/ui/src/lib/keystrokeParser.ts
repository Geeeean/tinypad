// Lets a macro be typed instead of built one step/click at a time, e.g.
// "cmd+c cmd+v cmd+/" -> three steps (Cmd+C, then Cmd+V, then Cmd+/).
// Grammar: whitespace-separated steps; each step is modifier names joined
// by "+", ending in exactly one key name (case-insensitive). A step with no
// "+" is just a bare key.

import { MACRO_KEY_LABELS, MacroModifier, type KeystrokeStep } from "@/types";

const MODIFIER_ALIASES: Record<string, MacroModifier> = {
  ctrl: MacroModifier.Ctrl,
  control: MacroModifier.Ctrl,
  alt: MacroModifier.Alt,
  option: MacroModifier.Alt,
  opt: MacroModifier.Alt,
  shift: MacroModifier.Shift,
  cmd: MacroModifier.Meta,
  command: MacroModifier.Meta,
  win: MacroModifier.Meta,
  windows: MacroModifier.Meta,
  super: MacroModifier.Meta,
  meta: MacroModifier.Meta,
};

// Every alias resolves to the same index space as MACRO_KEY_LABELS; built
// once from a-z/0-9/named keys/F1-F12 plus a couple of everyday synonyms
// (return/escape/del/bksp) beyond the canonical MACRO_KEY_LABELS spelling.
const KEY_ALIASES: Record<string, number> = (() => {
  const map: Record<string, number> = {};
  for (let i = 0; i < 26; i++) map[String.fromCharCode(97 + i)] = 1 + i; // a-z -> 1..26
  for (let i = 0; i <= 9; i++) map[String(i)] = 27 + i; // 0-9 -> 27..36
  map.space = 37;
  map.enter = 38;
  map.return = 38;
  map.tab = 39;
  map.esc = 40;
  map.escape = 40;
  map.backspace = 41;
  map.bksp = 41;
  map.delete = 42;
  map.del = 42;
  map.up = 43;
  map.down = 44;
  map.left = 45;
  map.right = 46;
  for (let i = 1; i <= 12; i++) map[`f${i}`] = 46 + i; // f1..f12 -> 47..58
  // Punctuation, appended after f1..f12 in types.ts's MACRO_KEY_LABELS --
  // the symbol itself is the token, e.g. "." or "cmd+/".
  const punctuation = [".", ",", "/", ";", "'", "-", "=", "[", "]", "\\", "`"];
  punctuation.forEach((sym, i) => (map[sym] = 59 + i)); // -> 59..69
  return map;
})();

const MODIFIER_TEXT: [MacroModifier, string][] = [
  [MacroModifier.Ctrl, "ctrl"],
  [MacroModifier.Alt, "alt"],
  [MacroModifier.Shift, "shift"],
  [MacroModifier.Meta, "cmd"],
];

// Parses one "+"-joined step, e.g. "cmd+shift+v" or a bare "g". The final
// segment must be a key name; everything before it must be a modifier name.
// Returns null if the token doesn't parse.
export function parseKeystrokeToken(token: string): KeystrokeStep | null {
  const parts = token
    .split("+")
    .map((p) => p.trim().toLowerCase())
    .filter(Boolean);
  if (parts.length === 0) {
    return null;
  }

  const keyPart = parts[parts.length - 1];
  const key = KEY_ALIASES[keyPart];
  if (key === undefined) {
    return null;
  }

  let modifiers = 0;
  for (const part of parts.slice(0, -1)) {
    const mod = MODIFIER_ALIASES[part];
    if (mod === undefined) {
      return null;
    }
    modifiers |= mod;
  }

  return { modifiers, key };
}

export interface ParsedKeystrokeSequence {
  steps: KeystrokeStep[];
  // One message per token that didn't parse, in order encountered.
  errors: string[];
}

export function parseKeystrokeSequence(text: string): ParsedKeystrokeSequence {
  const tokens = text.trim().split(/\s+/).filter(Boolean);
  const steps: KeystrokeStep[] = [];
  const errors: string[] = [];

  for (const token of tokens) {
    const parsed = parseKeystrokeToken(token);
    if (parsed) {
      steps.push(parsed);
    } else {
      errors.push(`"${token}" isn't a recognized key or modifier combo`);
    }
  }

  return { steps, errors };
}

function rawStepParts(step: KeystrokeStep): string[] {
  const modNames = MODIFIER_TEXT.filter(([bit]) => (step.modifiers & bit) !== 0).map(
    ([, name]) => name,
  );
  const keyName =
    step.key > 0 && step.key < MACRO_KEY_LABELS.length
      ? MACRO_KEY_LABELS[step.key].toLowerCase()
      : "";
  return [...modNames, keyName].filter(Boolean);
}

// Inverse of parseKeystrokeSequence -- used to populate the text box from
// steps the step-editor UI (or a loaded macro) already has, so both views
// of the same sequence stay round-trippable. Always spells a shifted
// punctuation glyph as "shift+<base key>" (e.g. "shift+="), never as the
// glyph itself (e.g. "+") -- the grammar can't accept "+" as a key token
// since "+" is the modifier-joining character, so anything typed here must
// stay parseable by parseKeystrokeToken. See formatKeystrokeStepParts below
// for the read-only display variant that isn't bound by that constraint.
export function stringifyKeystrokeSequence(steps: KeystrokeStep[]): string {
  return steps
    .map((step) => rawStepParts(step).join("+"))
    .filter(Boolean)
    .join(" ");
}

// The base punctuation key's shifted glyph, inverse of
// keystrokeCapture.ts's SHIFTED_PUNCTUATION_ALIASES (macro_key_t has no
// separate entry for it, e.g. no MACRO_KEY_PLUS -- "+" is stored as
// MACRO_KEY_EQUAL with Shift set).
const BASE_KEY_SHIFTED_GLYPH: Partial<Record<number, string>> = {
  59: ">", // period
  60: "<", // comma
  61: "?", // slash
  62: ":", // semicolon
  63: '"', // quote
  64: "_", // minus
  65: "+", // equal
  66: "{", // lbracket
  67: "}", // rbracket
  68: "|", // backslash
  69: "~", // grave
};

// Read-only display form of a single step, as its individual modifier/key
// parts (e.g. ["cmd", "+"], one per <Kbd> badge -- see KeystrokeEditor.tsx's
// StepChips) rather than one joined string. Unlike stringifyKeystrokeSequence,
// this renders a shifted punctuation glyph directly ("+" instead of "shift"
// + "="), since it's never fed back through parseKeystrokeToken.
export function formatKeystrokeStepParts(step: KeystrokeStep): string[] {
  const shiftedGlyph =
    (step.modifiers & MacroModifier.Shift) !== 0 ? BASE_KEY_SHIFTED_GLYPH[step.key] : undefined;
  if (shiftedGlyph === undefined) {
    return rawStepParts(step);
  }
  const modNames = MODIFIER_TEXT.filter(
    ([bit]) => bit !== MacroModifier.Shift && (step.modifiers & bit) !== 0,
  ).map(([, name]) => name);
  return [...modNames, shiftedGlyph];
}
