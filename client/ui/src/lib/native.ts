// Thin typed wrapper around the webview_bind()-generated globals from
// ui_bridge.c. Each native_* function returns a Promise resolved via
// webview_return(); failures are swallowed here (native side already logs
// them to stderr) so callers don't need a .catch() at every call site.

import type { KeystrokeStep, TinypadState } from "@/types";

declare global {
  interface Window {
    native_assign_slot: (slot: number, sessionId: number) => Promise<boolean>;
    native_clear_slot: (slot: number) => Promise<boolean>;
    native_set_slot_volume: (slot: number, volumePercent: number) => Promise<boolean>;
    native_toggle_slot_mute: (slot: number) => Promise<boolean>;
    native_set_macro: (...args: number[]) => Promise<boolean>;
    native_set_macro_label: (index: number, label: string) => Promise<boolean>;
    native_set_show_graph: (show: number) => Promise<boolean>;
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

export function setShowGraph(show: boolean): void {
  void window.native_set_show_graph(show ? 1 : 0).catch(ignoreRejection);
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
