import { useEffect, useMemo, useState } from "react";
import { Input } from "@/components/ui/input";
import { parseKeystrokeSequence, stringifyKeystrokeSequence } from "@/lib/keystrokeParser";
import type { KeystrokeStep } from "@/types";

interface KeystrokeEditorProps {
  steps: KeystrokeStep[];
  onCommit: (steps: KeystrokeStep[]) => void;
}

// Type the whole sequence as text (e.g. "cmd+c cmd+v cmd+/") instead of
// building it step-by-step with buttons. Parsed live for preview/error
// feedback; committed to the macro on blur or Enter, and only when it
// parses cleanly (a mid-typing invalid string never overwrites the last
// good binding).
export function KeystrokeEditor({ steps, onCommit }: KeystrokeEditorProps) {
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
    <div className="flex min-w-[220px] flex-col gap-1.5">
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
      ) : parsed.steps.length > 0 ? (
        <div className="flex flex-wrap gap-1">
          {parsed.steps.map((step, i) => (
            <span
              key={i}
              className="rounded bg-secondary px-1.5 py-0.5 font-mono text-[10px] text-secondary-foreground"
            >
              {stringifyKeystrokeSequence([step])}
            </span>
          ))}
        </div>
      ) : (
        <p className="text-[11px] text-muted-foreground">
          Space-separated steps, e.g. "cmd+c cmd+v cmd+/".
        </p>
      )}
    </div>
  );
}
