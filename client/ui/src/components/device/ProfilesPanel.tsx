import { useEffect, useState } from "react";
import { Check, Layers, Plus, Trash2 } from "lucide-react";
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { deleteProfile, renameProfile, saveProfile, saveProfileAs, switchProfile } from "@/lib/native";
import { cn } from "@/lib/utils";
import type { ProfileSummary } from "@/types";

interface ProfileRowProps {
  profile: ProfileSummary;
  active: boolean;
  canDelete: boolean;
  onSwitch: () => void;
  onDelete: () => void;
}

// A checkmark toggle (switches to this profile) + an inline-rename Input +
// a delete button. The checkmark is a separate control from the name field
// on purpose -- nesting the rename Input inside a clickable row/button
// would make typing into it also trigger the switch.
function ProfileRow({ profile, active, canDelete, onSwitch, onDelete }: ProfileRowProps) {
  const [nameText, setNameText] = useState(profile.name);
  const [focused, setFocused] = useState(false);

  useEffect(() => {
    if (!focused) {
      setNameText(profile.name);
    }
  }, [profile.name, focused]);

  function commitRename() {
    const trimmed = nameText.trim();
    if (trimmed && trimmed !== profile.name) {
      renameProfile(profile.id, trimmed);
    } else {
      setNameText(profile.name);
    }
  }

  return (
    <div
      className={cn(
        "flex items-center gap-2 rounded-lg border p-2 transition-colors",
        active ? "border-primary bg-secondary" : "border-border hover:border-primary/60",
      )}
    >
      <button
        type="button"
        onClick={onSwitch}
        className="flex size-6 shrink-0 items-center justify-center"
        title={active ? "Active profile" : "Switch to this profile"}
      >
        <Check className={cn("size-3.5", active ? "text-primary" : "text-transparent")} />
      </button>
      <Input
        value={nameText}
        className="h-7 flex-1 text-xs"
        onFocus={() => setFocused(true)}
        onChange={(e) => setNameText(e.target.value)}
        onBlur={() => {
          setFocused(false);
          commitRename();
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter") {
            commitRename();
            e.currentTarget.blur();
          }
        }}
      />
      <Button
        type="button"
        size="icon-sm"
        variant="outline"
        disabled={!canDelete}
        onClick={onDelete}
        title={canDelete ? "Delete profile" : "Can't delete the only profile"}
      >
        <Trash2 className="size-3.5" />
      </Button>
    </div>
  );
}

interface ProfilesPanelProps {
  profiles: ProfileSummary[];
  activeProfileId: number;
  dirty: boolean;
}

// Switch between saved macro/label/layout bundles, or save the current
// setup as a new one. Explicit save (matches the app's chosen model, see
// ui_bridge.c's profile_dirty) -- edits apply live immediately as always,
// but only reach the database on Save/Save as new, so switching or
// deleting while dirty confirms first via AlertDialog.
export function ProfilesPanel({ profiles, activeProfileId, dirty }: ProfilesPanelProps) {
  const [open, setOpen] = useState(false);
  const [newName, setNewName] = useState("");
  const [pendingSwitchId, setPendingSwitchId] = useState<number | null>(null);
  const [pendingDeleteId, setPendingDeleteId] = useState<number | null>(null);

  const activeProfile = profiles.find((p) => p.id === activeProfileId);

  function requestSwitch(id: number) {
    if (id === activeProfileId) {
      return;
    }
    if (dirty) {
      setPendingSwitchId(id);
    } else {
      void switchProfile(id);
    }
  }

  function confirmSwitch() {
    if (pendingSwitchId !== null) {
      void switchProfile(pendingSwitchId);
      setPendingSwitchId(null);
    }
  }

  function confirmDelete() {
    if (pendingDeleteId !== null) {
      void deleteProfile(pendingDeleteId);
      setPendingDeleteId(null);
    }
  }

  function createNew() {
    const name = newName.trim();
    if (!name) {
      return;
    }
    void saveProfileAs(name).then((ok) => {
      if (ok) {
        setNewName("");
      }
    });
  }

  return (
    <>
      <Button
        type="button"
        variant="outline"
        size="sm"
        className="gap-1.5 text-xs"
        onClick={() => setOpen(true)}
      >
        <Layers className="size-3.5" />
        {activeProfile?.name ?? "Profile"}
        {dirty && <span className="size-1.5 rounded-full bg-primary" title="Unsaved changes" />}
      </Button>

      <Dialog open={open} onOpenChange={setOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Profiles</DialogTitle>
            <DialogDescription>
              Switch between saved macro/label/layout bundles, or save the current setup as a new
              one.
            </DialogDescription>
          </DialogHeader>

          <div className="flex flex-col gap-1.5">
            {profiles.map((p) => (
              <ProfileRow
                key={p.id}
                profile={p}
                active={p.id === activeProfileId}
                canDelete={profiles.length > 1}
                onSwitch={() => requestSwitch(p.id)}
                onDelete={() => setPendingDeleteId(p.id)}
              />
            ))}
          </div>

          <div className="flex items-center justify-between gap-2 border-t pt-3">
            <span className="text-[11px] text-muted-foreground">
              {dirty ? "Unsaved changes to the active profile." : "No unsaved changes."}
            </span>
            <Button
              type="button"
              size="sm"
              variant="outline"
              className="text-xs"
              disabled={!dirty}
              onClick={() => void saveProfile()}
            >
              <Check className="size-3.5" />
              Save
            </Button>
          </div>

          <div className="flex items-center gap-2">
            <Input
              value={newName}
              placeholder="New profile name"
              className="h-8 flex-1 text-xs"
              onChange={(e) => setNewName(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === "Enter") {
                  createNew();
                }
              }}
            />
            <Button type="button" size="sm" variant="outline" className="text-xs" onClick={createNew}>
              <Plus className="size-3.5" />
              Save as new
            </Button>
          </div>
        </DialogContent>
      </Dialog>

      <AlertDialog
        open={pendingSwitchId !== null}
        onOpenChange={(o) => !o && setPendingSwitchId(null)}
      >
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Discard unsaved changes?</AlertDialogTitle>
            <AlertDialogDescription>
              Switching profiles now will lose your unsaved edits to the current one.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>Cancel</AlertDialogCancel>
            <AlertDialogAction onClick={confirmSwitch}>Discard &amp; switch</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>

      <AlertDialog
        open={pendingDeleteId !== null}
        onOpenChange={(o) => !o && setPendingDeleteId(null)}
      >
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Delete this profile?</AlertDialogTitle>
            <AlertDialogDescription>This can&apos;t be undone.</AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>Cancel</AlertDialogCancel>
            <AlertDialogAction onClick={confirmDelete}>Delete</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </>
  );
}
