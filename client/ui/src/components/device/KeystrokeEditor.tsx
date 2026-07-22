import { useEffect, useMemo, useState } from "react";
import { CircleDot, Keyboard, X } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { keyEventToStep } from "@/lib/keystrokeCapture";
import { parseKeystrokeSequence, stringifyKeystrokeSequence } from "@/lib/keystrokeParser";
import { MACRO_KEYSTROKE_MAX_STEPS, type KeystrokeStep } from "@/types";
import { cn } from "@/lib/utils";

interface KeystrokeEditorProps {
  steps: KeystrokeStep[];
  onCommit: (steps: KeystrokeStep[]) => void;
  // Lets the parent (KeyButton/KnobButton) suppress its dialog's
  // close-on-Escape behavior while a recording is in progress, since
  // Escape is itself a valid macro key to capture -- see RecordCapture
  // below.
  onRecordingChange?: (recording: boolean) => void;
}

type EditorMode = "record" | "type";

// Both editing modes -- press keys live, or type the "cmd+c cmd+v"
// grammar -- operate on the same KeystrokeStep[]/onCommit contract, so
// switching modes mid-edit never loses anything already committed.
export function KeystrokeEditor({ steps, onCommit, onRecordingChange }: KeystrokeEditorProps) {
  const [mode, setMode] = useState<EditorMode>("record");

  return (
    <div className="flex min-w-[220px] flex-col gap-1.5">
      <div className="flex items-center justify-end gap-0.5">
        <Button
          type="button"
          size="icon-xs"
          variant={mode === "record" ? "secondary" : "ghost"}
          title="Press keys to record"
          onClick={() => setMode("record")}
        >
          <CircleDot />
        </Button>
        <Button
          type="button"
          size="icon-xs"
          variant={mode === "type" ? "secondary" : "ghost"}
          title="Type the sequence as text"
          onClick={() => setMode("type")}
        >
          <Keyboard />
        </Button>
      </div>

      {mode === "record" ? (
        <RecordCapture steps={steps} onCommit={onCommit} onRecordingChange={onRecordingChange} />
      ) : (
        <TypeEditor steps={steps} onCommit={onCommit} />
      )}
    </div>
  );
}

// Type the whole sequence as text (e.g. "cmd+c cmd+v cmd+/") instead of
// pressing it. Parsed live for preview/error feedback; committed to the
// macro on blur or Enter, and only when it parses cleanly (a mid-typing
// invalid string never overwrites the last good binding).
function TypeEditor({ steps, onCommit }: Omit<KeystrokeEditorProps, "onRecordingChange">) {
  const [text, setText] = useState(() => stringifyKeystrokeSequence(steps));
  const [focused, setFocused] = useState(false);

  useEffect(() => {
    if (!focused) {
      setText(stringifyKeystrokeSequence(steps));
    }
  }, [steps, focused]);

  const parsed = useMemo(() => parseKeystrokeSequence(text), [text]);

  function commit() {
    if (parsed.errors.length === 0) {
      onCommit(parsed.steps);
    }
  }

  return (
    <>
      <Input
        value={text}
        placeholder="cmd+c cmd+v cmd+/"
        className="h-8 font-mono text-xs"
        onFocus={() => setFocused(true)}
        onChange={(e) => setText(e.target.value)}
        onBlur={() => {
          setFocused(false);
          commit();
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter") {
            commit();
            e.currentTarget.blur();
          }
        }}
      />
      {parsed.errors.length > 0 ? (
        <p className="text-[11px] text-destructive">{parsed.errors.join(", ")}</p>
      ) : (
        <StepChips steps={parsed.steps} emptyHint='Space-separated steps, e.g. "cmd+c cmd+v cmd+/".' />
      )}
    </>
  );
}

// Press the actual keys instead of typing them. Live while focused: every
// completed (non-modifier, non-repeat) keydown appends one step immediately
// -- click away to stop. Mistakes are fixed with each chip's own remove
// button below, not a keyboard shortcut, since every physical key you press
// (including Backspace/Delete/Escape) is itself a capturable macro step.
function RecordCapture({ steps, onCommit, onRecordingChange }: KeystrokeEditorProps) {
  const [recording, setRecording] = useState(false);

  return (
    <>
      <div
        role="textbox"
        aria-label="Press keys to record a macro step"
        tabIndex={0}
        onFocus={() => {
          setRecording(true);
          onRecordingChange?.(true);
        }}
        onBlur={() => {
          setRecording(false);
          onRecordingChange?.(false);
        }}
        onKeyDown={(e) => {
          e.preventDefault();
          e.stopPropagation();
          if (e.repeat || steps.length >= MACRO_KEYSTROKE_MAX_STEPS) {
            return;
          }
          const step = keyEventToStep(e);
          if (step) {
            onCommit([...steps, step]);
          }
        }}
        className={cn(
          "flex h-8 items-center rounded-md border border-input bg-transparent px-2 font-mono text-xs outline-none",
          "focus-visible:border-ring focus-visible:ring-3 focus-visible:ring-ring/50",
        )}
      >
        <span className="text-muted-foreground">
          {recording ? "Listening… press keys" : "Click, then press keys…"}
        </span>
      </div>
      <StepChips
        steps={steps}
        emptyHint="No steps recorded yet."
        onRemove={(index) => onCommit(steps.filter((_, i) => i !== index))}
      />
    </>
  );
}

interface StepChipsProps {
  steps: KeystrokeStep[];
  emptyHint: string;
  onRemove?: (index: number) => void;
}

function StepChips({ steps, emptyHint, onRemove }: StepChipsProps) {
  if (steps.length === 0) {
    return <p className="text-[11px] text-muted-foreground">{emptyHint}</p>;
  }

  return (
    <div className="flex flex-wrap gap-1">
      {steps.map((step, i) => (
        <span
          key={i}
          className="flex items-center gap-0.5 rounded bg-secondary py-0.5 pr-0.5 pl-1.5 font-mono text-[10px] text-secondary-foreground"
        >
          {stringifyKeystrokeSequence([step])}
          {onRemove && (
            <button
              type="button"
              onClick={() => onRemove(i)}
              className="rounded-sm p-0.5 hover:bg-destructive/20 hover:text-destructive"
              title="Remove this step"
            >
              <X className="size-2.5" />
            </button>
          )}
        </span>
      ))}
    </div>
  );
}
