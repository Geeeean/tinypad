// Mirrors the native state contract exactly:
//   - JSON shape: ui_bridge.c's build_state_json()
//   - macro_action_type_t / macro_trigger_t: include/core/macro_map.h
//   - macro_modifier_t / macro_key_t: include/platform/keyboard_inject.h
//   - MACRO_BUTTON_COUNT / MACRO_LABEL_LEN: shared/protocol.h
// Keep these in lockstep with the C headers -- there is no runtime
// validation tying them together, only this comment.

export const MacroActionType = {
  None: 0,
  ToggleMuteSlot: 1,
  Log: 2,
  SendKeystroke: 3,
} as const;
export type MacroActionType = (typeof MacroActionType)[keyof typeof MacroActionType];

export const MACRO_ACTION_LABELS: Record<MacroActionType, string> = {
  [MacroActionType.None]: "None",
  [MacroActionType.ToggleMuteSlot]: "Toggle mute",
  [MacroActionType.Log]: "Log (test)",
  [MacroActionType.SendKeystroke]: "Send keystroke",
};

export const MacroModifier = {
  Ctrl: 1 << 0,
  Alt: 1 << 1,
  Shift: 1 << 2,
  Meta: 1 << 3, // Cmd (macOS) / Win (Windows) / Super (Linux)
} as const;
export type MacroModifier = (typeof MacroModifier)[keyof typeof MacroModifier];

// Index order matters -- must stay identical to macro_key_t in
// keyboard_inject.h. Index 0 (MACRO_KEY_NONE) is a valid "no key" value.
export const MACRO_KEY_LABELS: readonly string[] = [
  "(none)",
  ...Array.from({ length: 26 }, (_, i) => String.fromCharCode(65 + i)), // A-Z
  ...Array.from({ length: 10 }, (_, i) => String(i)), // 0-9
  "Space", "Enter", "Tab", "Esc", "Backspace", "Delete", "Up", "Down", "Left", "Right",
  ...Array.from({ length: 12 }, (_, i) => `F${i + 1}`), // F1-F12
];

export const MACRO_KEY_NONE = 0;

// Index order matters -- must stay identical to macro_trigger_t.
export const MACRO_TRIGGER_LABELS = [
  "Switch 1", "Switch 2", "Switch 3", "Switch 4",
  "Switch 5", "Switch 6", "Switch 7", "Switch 8",
  "Encoder 1 button", "Encoder 2 button", "Encoder 3 button", "Encoder 4 button",
] as const;

export const SLOT_COUNT = 4;
export const MACRO_TRIGGER_COUNT = MACRO_TRIGGER_LABELS.length;
// Mirrors MACRO_KEYSTROKE_MAX_STEPS in include/core/macro_map.h.
export const MACRO_KEYSTROKE_MAX_STEPS = 16;
// Mirrors MACRO_BUTTON_COUNT in shared/protocol.h -- the 8 switches only,
// each with an on-device label box (encoder buttons have none).
export const MACRO_BUTTON_COUNT = 8;

export interface AudioSession {
  id: number;
  name: string;
  volume: number; // 0.0 - 1.0
  peak: number; // 0.0 - 1.0
  muted: boolean;
}

export interface MixerSlot {
  assigned: boolean;
  sessionId: number;
  name: string;
  volume: number; // 0-100
  peak: number; // 0-100
}

export interface KeystrokeStep {
  modifiers: number; // bitmask of MacroModifier
  key: number; // index into MACRO_KEY_LABELS
}

export interface MacroAction {
  type: MacroActionType;
  targetSlot: number;
  steps: KeystrokeStep[];
}

export interface DeviceSettings {
  showGraph: boolean;
  // Length MACRO_BUTTON_COUNT, index i is Switch (i+1)'s on-device label.
  macroLabels: string[];
}

export interface TinypadState {
  sessions: AudioSession[];
  slots: MixerSlot[];
  macros: MacroAction[];
  deviceSettings: DeviceSettings;
}

function emptyMacroAction(): MacroAction {
  return { type: MacroActionType.None, targetSlot: 0, steps: [] };
}

export const EMPTY_STATE: TinypadState = {
  sessions: [],
  slots: Array.from({ length: SLOT_COUNT }, () => ({
    assigned: false,
    sessionId: -1,
    name: "",
    volume: 0,
    peak: 0,
  })),
  macros: Array.from({ length: MACRO_TRIGGER_COUNT }, emptyMacroAction),
  deviceSettings: { showGraph: true, macroLabels: Array(MACRO_BUTTON_COUNT).fill("") },
};
