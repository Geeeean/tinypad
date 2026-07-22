import { useEffect, useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { MacroActionEditor } from "@/components/device/MacroActionEditor";
import { setMacroLabel } from "@/lib/native";
import { cn } from "@/lib/utils";
import { MacroActionType, type MacroAction } from "@/types";

interface KeyButtonProps {
  index: number; // 0-7 -- matches macro_trigger_t's SWITCH_(index+1) and the label index
  action: MacroAction;
  label: string;
}

// One physical switch: shows its custom label (or a default), click to
// rename it and choose what it does.
export function KeyButton({ index, action, label }: KeyButtonProps) {
  const [open, setOpen] = useState(false);
  const [labelText, setLabelText] = useState(label);
  const [focused, setFocused] = useState(false);
  const [recordingKeystroke, setRecordingKeystroke] = useState(false);

  useEffect(() => {
    if (!focused) {
      setLabelText(label);
    }
  }, [label, focused]);

  const displayLabel = label || `SW ${index + 1}`;
  const bound = action.type !== MacroActionType.None;

  function commitLabel() {
    setMacroLabel(index, labelText);
  }

  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        className={cn(
          "flex h-12 flex-col items-center justify-center rounded-md border text-[10px] font-semibold transition-colors",
          bound
            ? "border-primary/60 bg-secondary text-secondary-foreground hover:border-primary"
            : "border-border bg-muted text-muted-foreground hover:border-primary/60",
        )}
      >
        <span className="max-w-full truncate px-1">{displayLabel}</span>
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
            <DialogTitle>Switch {index + 1}</DialogTitle>
            <DialogDescription>Label shown on the device, and what it does.</DialogDescription>
          </DialogHeader>

          <div className="flex flex-col gap-1.5">
            <Label htmlFor={`key-label-${index}`}>Label</Label>
            <Input
              id={`key-label-${index}`}
              value={labelText}
              placeholder={`SW ${index + 1}`}
              maxLength={9}
              className="h-8 text-xs"
              onFocus={() => setFocused(true)}
              onChange={(e) => setLabelText(e.target.value)}
              onBlur={() => {
                setFocused(false);
                commitLabel();
              }}
              onKeyDown={(e) => {
                if (e.key === "Enter") {
                  commitLabel();
                  e.currentTarget.blur();
                }
              }}
            />
          </div>

          <div className="flex flex-col gap-2 border-t pt-3">
            <h3 className="text-xs font-semibold text-muted-foreground uppercase">Action</h3>
            <MacroActionEditor
              trigger={index}
              action={action}
              onKeystrokeRecordingChange={setRecordingKeystroke}
            />
          </div>
        </DialogContent>
      </Dialog>
    </>
  );
}
