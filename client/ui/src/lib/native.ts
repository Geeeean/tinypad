// Thin typed wrapper around the webview_bind()-generated globals from
// ui_bridge.c. Each native_* function returns a Promise resolved via
// webview_return(); failures are swallowed here (native side already logs
// them to stderr) so callers don't need a .catch() at every call site.

import { GUI_COMPONENT_COUNT, type KeystrokeStep, type TinypadState } from "@/types";

declare global {
  interface Window {
    native_assign_slot: (slot: number, sessionId: number) => Promise<boolean>;
    native_clear_slot: (slot: number) => Promise<boolean>;
    native_set_slot_volume: (slot: number, volumePercent: number) => Promise<boolean>;
    native_toggle_slot_mute: (slot: number) => Promise<boolean>;
    native_set_macro: (...args: number[]) => Promise<boolean>;
    native_set_macro_label: (index: number, label: string) => Promise<boolean>;
    native_set_gui_layout: (...layout: number[]) => Promise<boolean>;
    native_set_simulation_enabled: (enabled: number) => Promise<boolean>;
    native_set_simulated_level: (index: number, levelPercent: number) => Promise<boolean>;
    native_save_profile: () => Promise<boolean>;
    native_save_profile_as: (name: string) => Promise<boolean>;
    native_rename_profile: (id: number, name: string) => Promise<boolean>;
    native_delete_profile: (id: number) => Promise<boolean>;
    native_switch_profile: (id: number) => Promise<boolean>;
    onState?: (state: TinypadState) => void;
  }
}

function ignoreRejection(): void {
  // Errors are already logged natively; nothing actionable client-side.
}

export function assignSlot(slot: number, sessionId: number): void {
  void window.native_assign_slot(slot, sessionId).catch(ignoreRejection);
}

export function clearSlot(slot: number): void {
  void window.native_clear_slot(slot).catch(ignoreRejection);
}

export function setSlotVolume(slot: number, volumePercent: number): void {
  void window.native_set_slot_volume(slot, volumePercent).catch(ignoreRejection);
}

export function toggleSlotMute(slot: number): void {
  void window.native_toggle_slot_mute(slot).catch(ignoreRejection);
}

// Flattens [trigger, type, targetSlot, mod_1, key_1, mod_2, key_2, ...] --
// matches native_set_macro's parsing in ui_bridge.c exactly.
export function setMacro(
  trigger: number,
  type: number,
  targetSlot: number,
  steps: KeystrokeStep[],
): void {
  const args = [trigger, type, targetSlot];
  for (const step of steps) {
    args.push(step.modifiers, step.key);
  }
  void window.native_set_macro(...args).catch(ignoreRejection);
}

// index: 0..MACRO_BUTTON_COUNT-1 (the 8 switches only).
export function setMacroLabel(index: number, label: string): void {
  void window.native_set_macro_label(index, label).catch(ignoreRejection);
}

// layout: GUI_COMPONENT_COUNT GuiComponent ids in draw order; GUI_COMPONENT_NONE
// disables that slot.
export function setGuiLayout(layout: number[]): void {
  if (layout.length !== GUI_COMPONENT_COUNT) {
    throw new Error(`setGuiLayout: expected ${GUI_COMPONENT_COUNT} entries, got ${layout.length}`);
  }
  void window.native_set_gui_layout(...layout).catch(ignoreRejection);
}

// Swaps the mixer's audio source between real OS audio and audio_simulated's
// fake sessions ("detaching" from OS audio).
export function setSimulationEnabled(enabled: boolean): void {
  void window.native_set_simulation_enabled(enabled ? 1 : 0).catch(ignoreRejection);
}

// index: 0..MIXER_CHANNELS-1 (the simulated sessions), levelPercent: 0-100.
// Only takes effect while simulation is enabled.
export function setSimulatedLevel(index: number, levelPercent: number): void {
  void window.native_set_simulated_level(index, levelPercent).catch(ignoreRejection);
}

// Profile actions return their success/failure (unlike the fire-and-forget
// helpers above) since they have failure modes worth surfacing in the UI --
// a duplicate name on save-as/rename, or refusing to delete the last
// remaining profile.

// Overwrites the currently active profile with the current live state.
export function saveProfile(): Promise<boolean> {
  return window.native_save_profile().catch(() => false);
}

// Creates a new profile named `name` from the current live state and makes
// it active.
export function saveProfileAs(name: string): Promise<boolean> {
  return window.native_save_profile_as(name).catch(() => false);
}

export function renameProfile(id: number, name: string): Promise<boolean> {
  return window.native_rename_profile(id, name).catch(() => false);
}

export function deleteProfile(id: number): Promise<boolean> {
  return window.native_delete_profile(id).catch(() => false);
}

// Loads `id` into the live state, discarding any unsaved edits to whatever
// was active -- confirm with the user first if state.profileDirty is set.
export function switchProfile(id: number): Promise<boolean> {
  return window.native_switch_profile(id).catch(() => false);
}

// Registers window.onState (called by ui_bridge_push_state -> webview_eval,
// ~60 times a second) and returns an unsubscribe function.
export function subscribeState(onState: (state: TinypadState) => void): () => void {
  window.onState = onState;
  return () => {
    if (window.onState === onState) {
      window.onState = undefined;
    }
  };
}
