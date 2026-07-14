import type { AudioSession } from "@/types";

interface SessionsListProps {
  sessions: AudioSession[];
}

export function SessionsList({ sessions }: SessionsListProps) {
  return (
    <section className="space-y-2.5">
      <h2 className="text-xs font-semibold tracking-wider text-muted-foreground uppercase">
        Detected audio sessions
      </h2>
      {sessions.length === 0 ? (
        <p className="px-0.5 py-2 text-xs text-muted-foreground">No audio sessions detected yet.</p>
      ) : (
        <div className="divide-y divide-border overflow-hidden rounded-lg border">
          {sessions.map((s) => (
            <div key={s.id} className="flex items-center gap-2.5 bg-card px-2.5 py-2 text-xs">
              <span className="flex-1 truncate">
                {s.name}
                {s.muted ? " (muted)" : ""}
              </span>
              <span className="w-9 text-right font-mono text-muted-foreground tabular-nums">
                {Math.round(s.volume * 100)}%
              </span>
            </div>
          ))}
        </div>
      )}
    </section>
  );
}
