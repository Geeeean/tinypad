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
  // Appended after F12, matching macro_key_t in keyboard_inject.h -- keeps
  // every index above stable.
  ".", ",", "/", ";", "'", "-", "=", "[", "]", "\\", "`",
];

export const MACRO_KEY_NONE = 0;

// Index order matters -- must stay identical to macro_trigger_t.
export const MACRO_TRIGGER_LABELS = [
  "Switch 1", "Switch 2", "Switch 3", "Switch 4",
  "Switch 5", "Switch 6", "Switch 7", "Switch 8",
  "Encoder 1 button", "Encoder 2 button", "Encoder 3 button", "Encoder 4 button",
  // Rotation defaults to volume control (device_link.c) unless bound here --
  // e.g. spin-right could fire a macro while spin-left still adjusts volume.
  "Encoder 1 rotate right", "Encoder 1 rotate left",
  "Encoder 2 rotate right", "Encoder 2 rotate left",
  "Encoder 3 rotate right", "Encoder 3 rotate left",
  "Encoder 4 rotate right", "Encoder 4 rotate left",
] as const;

export const SLOT_COUNT = 4;
// Mirrors MIXER_MASTER_SESSION_ID in include/core/mixer_state.h -- assigning
// a slot to this id binds it to the system's total/master output instead of
// any one AudioSession from the live `sessions` list.
export const MIXER_MASTER_SESSION_ID = 0xfffffffe;
export const MACRO_TRIGGER_COUNT = MACRO_TRIGGER_LABELS.length;

// Index into MACRO_TRIGGER_LABELS/state.macros for a knob's rotate-right/
// rotate-left binding -- mirrors MACRO_TRIGGER_ENCODER_n_ROTATE_PLUS/MINUS's
// layout in macro_map.h (appended after the 12 switch/button triggers, 2
// per encoder).
export function encoderRotatePlusTrigger(index: number): number {
  return 12 + index * 2;
}
export function encoderRotateMinusTrigger(index: number): number {
  return 13 + index * 2;
}
// Mirrors MACRO_KEYSTROKE_MAX_STEPS in include/core/macro_map.h.
export const MACRO_KEYSTROKE_MAX_STEPS = 64;
// Mirrors MACRO_BUTTON_COUNT in shared/protocol.h -- the 8 switches only,
// each with an on-device label box (encoder buttons have none).
export const MACRO_BUTTON_COUNT = 8;

// Mirrors the GUI_COMPONENT_* enum in shared/protocol.h: the device
// dashboard pieces that can be independently enabled and reordered via
// DeviceSettings.guiLayout.
export const GuiComponent = {
  VuMeters: 0,
  Waveform: 1,
  MacroGrid: 2,
  ChannelRows: 3,
} as const;
export type GuiComponent = (typeof GuiComponent)[keyof typeof GuiComponent];

export const GUI_COMPONENT_LABELS: Record<GuiComponent, string> = {
  [GuiComponent.VuMeters]: "VU Meters",
  [GuiComponent.Waveform]: "Waveform",
  [GuiComponent.MacroGrid]: "Macro Grid",
  [GuiComponent.ChannelRows]: "Channel Rows",
};

// Mirrors GUI_COMPONENT_COUNT in shared/protocol.h.
export const GUI_COMPONENT_COUNT = 4;
// Mirrors GUI_COMPONENT_NONE in shared/protocol.h -- a guiLayout slot
// holding this id is disabled.
export const GUI_COMPONENT_NONE = 0xff;

// Mirrors the TOPBAR_ITEM_* enum in shared/protocol.h: what each of the
// topbar's 3 fixed slots can independently show, via DeviceSettings.topbarItems.
export const TopbarItem = {
  Connection: 0,
  AnyMuted: 1,
  ClipWarning: 2,
  ActiveChannels: 3,
  OutputLevel: 4,
  LoudestChannel: 5,
  Uptime: 6,
  FineStep: 7,
  MasterVolume: 8,
  ProfileName: 9,
  SessionCount: 10,
  Clock: 11,
} as const;
export type TopbarItem = (typeof TopbarItem)[keyof typeof TopbarItem];

export const TOPBAR_ITEM_LABELS: Record<TopbarItem, string> = {
  [TopbarItem.Connection]: "Connection status",
  [TopbarItem.AnyMuted]: "Any-muted indicator",
  [TopbarItem.ClipWarning]: "Clip warning",
  [TopbarItem.ActiveChannels]: "Active channel count",
  [TopbarItem.OutputLevel]: "Combined output level",
  [TopbarItem.LoudestChannel]: "Loudest channel",
  [TopbarItem.Uptime]: "Device uptime",
  [TopbarItem.FineStep]: "Fine-step mode indicator",
  [TopbarItem.MasterVolume]: "Master volume/mute",
  [TopbarItem.ProfileName]: "Active profile name",
  [TopbarItem.SessionCount]: "Known session count",
  [TopbarItem.Clock]: "Clock",
};

// Mirrors TOPBAR_SLOT_COUNT in shared/protocol.h.
export const TOPBAR_SLOT_COUNT = 3;
// Mirrors TOPBAR_ITEM_NONE in shared/protocol.h -- a topbarItems slot
// holding this id is disabled.
export const TOPBAR_ITEM_NONE = 0xff;

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
  muted: boolean;
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
  // Length GUI_COMPONENT_COUNT, GuiComponent ids in draw order (top to
  // bottom); a slot holding GUI_COMPONENT_NONE disables that piece.
  guiLayout: number[];
  // Length MACRO_BUTTON_COUNT, index i is Switch (i+1)'s on-device label.
  macroLabels: string[];
  // Length TOPBAR_SLOT_COUNT, TopbarItem ids for the topbar's 3 fixed
  // positions; a slot holding TOPBAR_ITEM_NONE disables that position.
  topbarItems: number[];
}

export interface ProfileSummary {
  id: number;
  name: string;
}

export interface TinypadState {
  sessions: AudioSession[];
  slots: MixerSlot[];
  macros: MacroAction[];
  deviceSettings: DeviceSettings;
  // Whether the mixer's audio source is audio_simulated's fake sessions
  // (true) or real OS audio (false).
  simulationEnabled: boolean;
  profiles: ProfileSummary[];
  activeProfileId: number;
  // Whether macros/deviceSettings/slot assignments have changed since the
  // active profile was last saved -- explicit save, not auto-save.
  profileDirty: boolean;
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
    muted: false,
  })),
  macros: Array.from({ length: MACRO_TRIGGER_COUNT }, emptyMacroAction),
  deviceSettings: {
    guiLayout: [GuiComponent.VuMeters, GuiComponent.Waveform, GuiComponent.MacroGrid],
    macroLabels: Array(MACRO_BUTTON_COUNT).fill(""),
    topbarItems: Array(TOPBAR_SLOT_COUNT).fill(TOPBAR_ITEM_NONE),
  },
  simulationEnabled: false,
  profiles: [],
  activeProfileId: -1,
  profileDirty: false,
};
