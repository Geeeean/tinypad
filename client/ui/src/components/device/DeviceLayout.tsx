import { DisplayPanel } from "@/components/device/DisplayPanel";
import { KeyButton } from "@/components/device/KeyButton";
import { KnobButton } from "@/components/device/KnobButton";
import type { TinypadState } from "@/types";

interface DeviceLayoutProps {
  state: TinypadState;
}

// A top-down mockup of the physical device: display, then the row of 4
// knobs, then the 2x4 key grid -- matching the real PCB layout. Every
// element is clickable and opens the relevant configuration.
export function DeviceLayout({ state }: DeviceLayoutProps) {
  return (
    <div className="rounded-2xl border bg-card p-6 shadow-sm">
      <DisplayPanel settings={state.deviceSettings} />

      <div className="mt-6 flex justify-center gap-6">
        {state.slots.map((slot, i) => (
          <KnobButton
            key={i}
            index={i}
            slot={slot}
            sessions={state.sessions}
            buttonAction={state.macros[8 + i]}
          />
        ))}
      </div>

      <div className="mt-6 grid grid-cols-4 gap-3">
        {Array.from({ length: 8 }, (_, i) => (
          <KeyButton
            key={i}
            index={i}
            action={state.macros[i]}
            label={state.deviceSettings.macroLabels[i] ?? ""}
          />
        ))}
      </div>
    </div>
  );
}
