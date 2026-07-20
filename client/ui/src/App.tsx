import { DeviceLayout } from "@/components/device/DeviceLayout";
import { ProfilesPanel } from "@/components/device/ProfilesPanel";
import { SessionsList } from "@/components/device/SessionsList";
import { SimulationPanel } from "@/components/device/SimulationPanel";
import { useTinypadState } from "@/hooks/useTinypadState";

export default function App() {
  const state = useTinypadState();

  return (
    <div className="mx-auto max-w-[520px] px-6 py-5 pb-10">
      <header className="mb-5 flex items-center justify-between">
        <div className="flex items-baseline gap-2">
          <h1 className="text-lg font-semibold tracking-wide">TINYPAD</h1>
          <span className="text-xs text-muted-foreground">mixer &amp; macro pad</span>
        </div>
        <div className="flex items-center gap-2">
          <ProfilesPanel
            profiles={state.profiles}
            activeProfileId={state.activeProfileId}
            dirty={state.profileDirty}
          />
          <SimulationPanel enabled={state.simulationEnabled} />
        </div>
      </header>

      <div className="space-y-7">
        <DeviceLayout state={state} />
        <SessionsList sessions={state.sessions} />
      </div>
    </div>
  );
}
