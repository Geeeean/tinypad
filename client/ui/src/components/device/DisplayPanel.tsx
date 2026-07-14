import { useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import { setShowGraph } from "@/lib/native";
import type { DeviceSettings } from "@/types";

interface DisplayPanelProps {
  settings: DeviceSettings;
}

// The physical screen -- click it to change what's shown there.
export function DisplayPanel({ settings }: DisplayPanelProps) {
  const [open, setOpen] = useState(false);

  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        className="flex h-28 w-full flex-col items-center justify-center gap-1 rounded-lg border-2 border-border bg-black transition-colors hover:border-primary"
      >
        <span className="font-mono text-xs tracking-[0.2em] text-muted-foreground">DISPLAY</span>
        <span className="text-[10px] text-muted-foreground">
          waveform graph {settings.showGraph ? "on" : "off"}
        </span>
      </button>

      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Display</DialogTitle>
            <DialogDescription>Settings for the on-device screen.</DialogDescription>
          </DialogHeader>
          <div className="flex items-center justify-between">
            <Label htmlFor="show-graph">Show waveform graph</Label>
            <Switch
              id="show-graph"
              checked={settings.showGraph}
              onCheckedChange={(checked) => setShowGraph(checked)}
            />
          </div>
        </DialogContent>
      </Dialog>
    </>
  );
}
