import { useState } from "react";
import { Radio } from "lucide-react";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Label } from "@/components/ui/label";
import { Slider } from "@/components/ui/slider";
import { Switch } from "@/components/ui/switch";
import { setSimulatedLevel, setSimulationEnabled } from "@/lib/native";
import { SLOT_COUNT } from "@/types";

interface SimulationPanelProps {
  enabled: boolean;
}

// Detaches the mixer from real OS audio and drives fake "Simulated N"
// sessions instead (native_set_simulation_enabled -> audio_simulated
// backend). Enabling it auto-assigns all 4 fake sessions to their matching
// knob (native side), so VU activity shows up on the device immediately --
// the sliders here are the one thing real sessions don't need from the UI,
// their peak/VU level, since there's no real audio to derive it from.
export function SimulationPanel({ enabled }: SimulationPanelProps) {
  const [open, setOpen] = useState(false);
  // Mirrors audio_simulated.c's sim_create() default (60%) so this slider's
  // starting position matches the level already in effect -- simulation
  // produces audible-looking activity as soon as it's enabled, no slider
  // drag required first.
  const [levels, setLevels] = useState<number[]>(() => Array(SLOT_COUNT).fill(60));

  return (
    <>
      <Button
        type="button"
        variant={enabled ? "default" : "outline"}
        size="sm"
        className="gap-1.5 text-xs"
        onClick={() => setOpen(true)}
      >
        <Radio className="size-3.5" />
        Simulation{enabled ? " (on)" : ""}
      </Button>

      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Simulation</DialogTitle>
            <DialogDescription>
              Detach from real OS audio and drive fake sessions instead. Enabling this assigns 4
              "Simulated" sessions to the knobs automatically, so the device's VU meters start
              moving right away -- use the sliders below to adjust each channel's level.
            </DialogDescription>
          </DialogHeader>

          <div className="flex items-center gap-2">
            <Switch
              id="simulation-enabled"
              checked={enabled}
              onCheckedChange={(checked) => setSimulationEnabled(checked)}
            />
            <Label htmlFor="simulation-enabled">Enable simulation</Label>
          </div>

          {enabled && (
            <div className="flex flex-col gap-2 border-t pt-3">
              <h3 className="text-xs font-semibold text-muted-foreground uppercase">Levels</h3>
              {levels.map((level, i) => (
                <div key={i} className="flex items-center gap-2">
                  <span className="w-20 shrink-0 text-xs text-muted-foreground">
                    Simulated {i + 1}
                  </span>
                  <Slider
                    value={[level]}
                    max={100}
                    step={1}
                    onValueChange={(value) => {
                      const v = Array.isArray(value) ? value[0] : value;
                      setLevels((prev) => prev.map((l, idx) => (idx === i ? v : l)));
                      setSimulatedLevel(i, v);
                    }}
                    className="flex-1"
                  />
                  <span className="w-8 text-right font-mono text-xs text-muted-foreground tabular-nums">
                    {level}%
                  </span>
                </div>
              ))}
            </div>
          )}
        </DialogContent>
      </Dialog>
    </>
  );
}
