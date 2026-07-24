import { KeystrokeEditor } from "@/components/device/KeystrokeEditor";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { setMacro } from "@/lib/native";
import {
  MACRO_ACTION_LABELS,
  MacroActionType,
  SLOT_COUNT,
  type KeystrokeStep,
  type MacroAction,
} from "@/types";

interface MacroActionEditorProps {
  trigger: number;
  action: MacroAction;
  // Forwarded to KeystrokeEditor -- lets the parent dialog suppress its own
  // close-on-Escape while a keystroke recording is in progress.
  onKeystrokeRecordingChange?: (recording: boolean) => void;
  // Overrides MacroActionType.None's label -- e.g. an encoder's rotate
  // trigger doesn't literally do "nothing" when unbound, it falls back to
  // adjusting volume (see device_link.c's apply_command()), so the picker
  // should say that instead of a generic "None".
  noneLabel?: string;
}

// The "what does this button do" half of a key/knob-button's config
// dialog -- action type, plus whichever detail that type needs (a target
// channel, or a keystroke sequence). Shared between switches (KeyButton)
// and encoder buttons (KnobButton), which both bind to a macro_trigger_t.
export function MacroActionEditor({
  trigger,
  action,
  onKeystrokeRecordingChange,
  noneLabel,
}: MacroActionEditorProps) {
  const targetSlot = Math.max(0, action.targetSlot);

  function labelFor(type: MacroActionType): string {
    return type === MacroActionType.None && noneLabel ? noneLabel : MACRO_ACTION_LABELS[type];
  }

  function setType(type: MacroActionType) {
    setMacro(trigger, type, targetSlot, action.steps);
  }
  function setTargetSlot(slot: number) {
    setMacro(trigger, action.type, slot, action.steps);
  }
  function setSteps(steps: KeystrokeStep[]) {
    setMacro(trigger, action.type, targetSlot, steps);
  }

  return (
    <div className="flex flex-col gap-2">
      <Select value={String(action.type)} onValueChange={(v) => setType(Number(v) as MacroActionType)}>
        <SelectTrigger size="sm" className="w-full text-xs">
          <SelectValue>{labelFor(action.type)}</SelectValue>
        </SelectTrigger>
        <SelectContent>
          {Object.entries(MACRO_ACTION_LABELS).map(([value]) => (
            <SelectItem key={value} value={value}>
              {labelFor(Number(value) as MacroActionType)}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>

      {action.type === MacroActionType.ToggleMuteSlot && (
        <Select value={String(targetSlot)} onValueChange={(v) => setTargetSlot(Number(v))}>
          <SelectTrigger size="sm" className="w-full text-xs">
            <SelectValue>{`Channel ${targetSlot + 1}`}</SelectValue>
          </SelectTrigger>
          <SelectContent>
            {Array.from({ length: SLOT_COUNT }, (_, i) => (
              <SelectItem key={i} value={String(i)}>{`Channel ${i + 1}`}</SelectItem>
            ))}
          </SelectContent>
        </Select>
      )}

      {action.type === MacroActionType.SendKeystroke && (
        <KeystrokeEditor
          steps={action.steps}
          onCommit={setSteps}
          onRecordingChange={onKeystrokeRecordingChange}
        />
      )}
    </div>
  );
}
