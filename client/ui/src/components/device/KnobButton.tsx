import { useEffect, useState } from "react";
import { Volume2, VolumeX } from "lucide-react";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Slider } from "@/components/ui/slider";
import { MacroActionEditor } from "@/components/device/MacroActionEditor";
import { assignSlot, clearSlot, setSlotVolume, toggleSlotMute } from "@/lib/native";
import {
  encoderRotateMinusTrigger,
  encoderRotatePlusTrigger,
  MIXER_MASTER_SESSION_ID,
  type AudioSession,
  type MacroAction,
  type MixerSlot,
} from "@/types";

// Tailwind has no directional border *color* utility, and this needs just
// one side worth (the whole ring, actually, but still a single custom
// color per knob) -- inline style referencing the raw --slot-N custom
// property is simpler and more reliable than fighting arbitrary-value
// class generation for it. See index.css's :root/.dark for the values.
const SLOT_ACCENT_COLORS = ["var(--slot-0)", "var(--slot-1)", "var(--slot-2)", "var(--slot-3)"];
const NONE_VALUE = "none";

interface KnobButtonProps {
  index: number; // 0-3
  slot: MixerSlot;
  sessions: AudioSession[];
  buttonAction: MacroAction; // the encoder's press action (trigger = 8 + index)
  // Rotation defaults to adjusting this slot's volume (handled directly by
  // firmware/device_link) unless one of these is bound to something other
  // than MacroActionType.None, in which case that macro fires instead.
  rotatePlusAction: MacroAction;
  rotateMinusAction: MacroAction;
}

// One physical rotary encoder: turning it controls a channel's volume
// (handled directly by firmware/device_link, not configurable here);
// clicking it opens what channel it's assigned to and what pressing it
// down does.
export function KnobButton({
  index,
  slot,
  sessions,
  buttonAction,
  rotatePlusAction,
  rotateMinusAction,
}: KnobButtonProps) {
  const [open, setOpen] = useState(false);
  const [localVolume, setLocalVolume] = useState(slot.volume);
  const [dragging, setDragging] = useState(false);
  const [recordingKeystroke, setRecordingKeystroke] = useState(false);

  useEffect(() => {
    if (!dragging) {
      setLocalVolume(slot.volume);
    }
  }, [slot.volume, dragging]);

  // slot.muted (not a sessions[] lookup by id) since Master is never in the
  // live sessions list -- only mixer_state's own slot sync tracks its mute
  // state.
  const muted = slot.assigned && slot.muted;
  const accent = SLOT_ACCENT_COLORS[index % SLOT_ACCENT_COLORS.length];

  return (
    <>
      <button type="button" onClick={() => setOpen(true)} className="flex flex-col items-center gap-1.5">
        <div
          className="relative flex size-16 items-center justify-center rounded-full border-4 bg-secondary transition-colors hover:brightness-110"
          style={{ borderColor: accent }}
        >
          {muted && (
            <VolumeX className="absolute -top-1 -right-1 size-4 rounded-full bg-destructive p-0.5 text-white" />
          )}
          <span className="text-[10px] font-semibold text-muted-foreground">{localVolume}%</span>
        </div>
        <span className="max-w-16 truncate text-[10px] text-muted-foreground">
          {slot.assigned ? slot.name || "(unnamed)" : "Unassigned"}
        </span>
      </button>

      <Dialog
        open={open}
        onOpenChange={(nextOpen, eventDetails) => {
          // Escape is itself a valid macro key -- don't let it also close
          // the dialog while KeystrokeEditor's record mode is listening.
          if (eventDetails.reason === "escape-key" && recordingKeystroke) {
            eventDetails.cancel();
            return;
          }
          setOpen(nextOpen);
        }}
      >
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Knob {index + 1}</DialogTitle>
            <DialogDescription>Channel assignment and what pressing it does.</DialogDescription>
          </DialogHeader>

          <div className="flex flex-col gap-2">
            <h3 className="text-xs font-semibold text-muted-foreground uppercase">Channel</h3>
            <Select
              value={slot.assigned ? String(slot.sessionId) : NONE_VALUE}
              onValueChange={(value) => {
                if (value === NONE_VALUE) {
                  clearSlot(index);
                } else {
                  assignSlot(index, Number(value));
                }
              }}
            >
              <SelectTrigger size="sm" className="w-full text-xs">
                <SelectValue>{slot.assigned ? slot.name || "(unnamed)" : "— none —"}</SelectValue>
              </SelectTrigger>
              <SelectContent>
                <SelectItem value={NONE_VALUE}>— none —</SelectItem>
                <SelectItem value={String(MIXER_MASTER_SESSION_ID)}>Master</SelectItem>
                {sessions.map((s) => (
                  <SelectItem key={s.id} value={String(s.id)}>
                    {s.name}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>

            <div className="flex items-center gap-2">
              <Slider
                value={[localVolume]}
                max={100}
                step={1}
                onValueChange={(value) => {
                  const v = Array.isArray(value) ? value[0] : value;
                  setDragging(true);
                  setLocalVolume(v);
                  setSlotVolume(index, v);
                }}
                onValueCommitted={() => setDragging(false)}
                className="flex-1"
              />
              <span className="w-8 text-right font-mono text-xs text-muted-foreground tabular-nums">
                {localVolume}%
              </span>
              <Button
                type="button"
                size="icon"
                variant={muted ? "destructive" : "outline"}
                className="size-7"
                onClick={() => toggleSlotMute(index)}
                title="Toggle mute"
              >
                {muted ? <VolumeX className="size-3.5" /> : <Volume2 className="size-3.5" />}
              </Button>
            </div>
          </div>

          <div className="flex flex-col gap-2 border-t pt-3">
            <h3 className="text-xs font-semibold text-muted-foreground uppercase">Button (press)</h3>
            <MacroActionEditor
              trigger={8 + index}
              action={buttonAction}
              onKeystrokeRecordingChange={setRecordingKeystroke}
            />
          </div>

          <div className="flex flex-col gap-2 border-t pt-3">
            <h3 className="text-xs font-semibold text-muted-foreground uppercase">Rotate right</h3>
            <MacroActionEditor
              trigger={encoderRotatePlusTrigger(index)}
              action={rotatePlusAction}
              onKeystrokeRecordingChange={setRecordingKeystroke}
              noneLabel="Increase volume"
            />
          </div>

          <div className="flex flex-col gap-2 border-t pt-3">
            <h3 className="text-xs font-semibold text-muted-foreground uppercase">Rotate left</h3>
            <MacroActionEditor
              trigger={encoderRotateMinusTrigger(index)}
              action={rotateMinusAction}
              onKeystrokeRecordingChange={setRecordingKeystroke}
              noneLabel="Decrease volume"
            />
          </div>
        </DialogContent>
      </Dialog>
    </>
  );
}
