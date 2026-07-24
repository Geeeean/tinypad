import { useState } from "react";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Label } from "@/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { setTopbarItems } from "@/lib/native";
import {
  TOPBAR_ITEM_LABELS,
  TOPBAR_ITEM_NONE,
  TOPBAR_SLOT_COUNT,
  TopbarItem,
  type DeviceSettings,
} from "@/types";

interface TopbarPanelProps {
  settings: DeviceSettings;
}

const NONE_VALUE = "none";
const ALL_ITEMS = Object.values(TopbarItem) as TopbarItem[];

// The device's topbar has exactly TOPBAR_SLOT_COUNT fixed positions (unlike
// DisplayPanel's variable-length reorderable stack), so each gets its own
// independent dropdown rather than a toggle+reorder list.
export function TopbarPanel({ settings }: TopbarPanelProps) {
  const [open, setOpen] = useState(false);

  const items = settings.topbarItems;
  const enabledCount = items.filter((id) => id !== TOPBAR_ITEM_NONE).length;

  function setSlot(slotIndex: number, value: string | null) {
    if (value === null) return;
    const next = [...items];
    next[slotIndex] = value === NONE_VALUE ? TOPBAR_ITEM_NONE : Number(value);
    setTopbarItems(next);
  }

  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        className="mt-2 flex h-8 w-full items-center justify-center gap-2 rounded-lg border border-border bg-black transition-colors hover:border-primary"
      >
        <span className="font-mono text-[10px] tracking-[0.2em] text-muted-foreground">TOPBAR</span>
        <span className="text-[10px] text-muted-foreground">
          {enabledCount} of {TOPBAR_SLOT_COUNT} slots on
        </span>
      </button>

      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Topbar</DialogTitle>
            <DialogDescription>
              Choose what each of the 3 topbar slots shows on the device screen.
            </DialogDescription>
          </DialogHeader>
          <div className="flex flex-col gap-2">
            {Array.from({ length: TOPBAR_SLOT_COUNT }, (_, slotIndex) => {
              const value = items[slotIndex] ?? TOPBAR_ITEM_NONE;
              return (
                <div key={slotIndex} className="flex flex-col gap-1">
                  <Label htmlFor={`topbar-slot-${slotIndex}`}>Slot {slotIndex + 1}</Label>
                  <Select
                    value={value === TOPBAR_ITEM_NONE ? NONE_VALUE : String(value)}
                    onValueChange={(next) => setSlot(slotIndex, next)}
                  >
                    <SelectTrigger id={`topbar-slot-${slotIndex}`} size="sm" className="w-full text-xs">
                      <SelectValue>
                        {value === TOPBAR_ITEM_NONE ? "— none —" : TOPBAR_ITEM_LABELS[value as TopbarItem]}
                      </SelectValue>
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value={NONE_VALUE}>— none —</SelectItem>
                      {ALL_ITEMS.map((id) => (
                        <SelectItem key={id} value={String(id)}>
                          {TOPBAR_ITEM_LABELS[id]}
                        </SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                </div>
              );
            })}
          </div>
        </DialogContent>
      </Dialog>
    </>
  );
}
