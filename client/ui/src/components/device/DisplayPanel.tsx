import { ChevronDown, ChevronUp } from "lucide-react";
import { useState } from "react";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import { setGuiLayout } from "@/lib/native";
import {
  GUI_COMPONENT_COUNT,
  GUI_COMPONENT_LABELS,
  GUI_COMPONENT_NONE,
  GuiComponent,
  type DeviceSettings,
} from "@/types";

interface DisplayPanelProps {
  settings: DeviceSettings;
}

const ALL_COMPONENTS: GuiComponent[] = [
  GuiComponent.VuMeters,
  GuiComponent.Waveform,
  GuiComponent.MacroGrid,
];

// Pads/truncates an enabled-id list (in draw order) out to the wire array
// shape: GUI_COMPONENT_COUNT slots, unused ones filled with the "disabled"
// sentinel. Position among the trailing NONEs doesn't matter -- only the
// enabled ids' relative order does.
function toGuiLayout(enabledOrder: number[]): number[] {
  const layout = enabledOrder.slice(0, GUI_COMPONENT_COUNT);
  while (layout.length < GUI_COMPONENT_COUNT) {
    layout.push(GUI_COMPONENT_NONE);
  }
  return layout;
}

// The physical screen -- click it to change what's shown there, and in
// what order.
export function DisplayPanel({ settings }: DisplayPanelProps) {
  const [open, setOpen] = useState(false);

  // guiLayout's slot *position* is draw order, not identity -- a disabled
  // component's id doesn't survive being cleared to GUI_COMPONENT_NONE, so
  // rows are built from "which of the 3 known ids currently appear" rather
  // than iterating slots directly. That also means toggling a component back
  // on always re-appends it at the end of the draw order, since its old
  // position wasn't retained anywhere.
  const enabledOrder = settings.guiLayout.filter((id) => id !== GUI_COMPONENT_NONE);
  const rows = [
    ...enabledOrder,
    ...ALL_COMPONENTS.filter((id) => !enabledOrder.includes(id)),
  ] as GuiComponent[];

  function toggle(id: GuiComponent, checked: boolean) {
    const next = checked ? [...enabledOrder, id] : enabledOrder.filter((c) => c !== id);
    setGuiLayout(toGuiLayout(next));
  }

  function move(id: GuiComponent, delta: number) {
    const index = enabledOrder.indexOf(id);
    const target = index + delta;
    if (index < 0 || target < 0 || target >= enabledOrder.length) return;
    const next = [...enabledOrder];
    [next[index], next[target]] = [next[target], next[index]];
    setGuiLayout(toGuiLayout(next));
  }

  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        className="flex h-28 w-full flex-col items-center justify-center gap-1 rounded-lg border-2 border-border bg-black transition-colors hover:border-primary"
      >
        <span className="font-mono text-xs tracking-[0.2em] text-muted-foreground">DISPLAY</span>
        <span className="text-[10px] text-muted-foreground">
          {enabledOrder.length} of {ALL_COMPONENTS.length} pieces on
        </span>
      </button>

      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Display</DialogTitle>
            <DialogDescription>
              Choose which dashboard pieces show on the device screen, and in what order (top to
              bottom).
            </DialogDescription>
          </DialogHeader>
          <div className="flex flex-col gap-2">
            {rows.map((id) => {
              const enabled = enabledOrder.includes(id);
              return (
                <div
                  key={id}
                  className="flex items-center justify-between gap-2 rounded-lg border border-border p-2"
                >
                  <div className="flex items-center gap-2">
                    <Switch
                      id={`gui-component-${id}`}
                      checked={enabled}
                      onCheckedChange={(checked) => toggle(id, checked)}
                    />
                    <Label htmlFor={`gui-component-${id}`}>{GUI_COMPONENT_LABELS[id]}</Label>
                  </div>
                  <div className="flex gap-1">
                    <Button
                      variant="outline"
                      size="icon-sm"
                      disabled={!enabled || enabledOrder.indexOf(id) === 0}
                      onClick={() => move(id, -1)}
                    >
                      <ChevronUp />
                    </Button>
                    <Button
                      variant="outline"
                      size="icon-sm"
                      disabled={!enabled || enabledOrder.indexOf(id) === enabledOrder.length - 1}
                      onClick={() => move(id, 1)}
                    >
                      <ChevronDown />
                    </Button>
                  </div>
                </div>
              );
            })}
          </div>
        </DialogContent>
      </Dialog>
    </>
  );
}
