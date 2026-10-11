"use client";

import { useEffect, useRef } from "react";
import type { LogEntry } from "@/types/stats";

interface Props {
  logs: LogEntry[];
}

const LEVEL_CLASS: Record<LogEntry["level"], string> = {
  INFO: "text-muted-foreground",
  WARN: "text-yellow-700 dark:text-yellow-300",
  ERR: "text-red-700 dark:text-red-400",
};

function fmtTs(d: Date) {
  const p = (n: number) => String(n).padStart(2, "0");
  return `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

export default function EventLog({ logs }: Props) {
  const bodyRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const el = bodyRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [logs]);

  return (
    <div
      ref={bodyRef}
      role="log"
      aria-label="Dashboard event log"
      tabIndex={0}
      className="h-72 overflow-y-auto rounded-xl border border-card-edge bg-card-fill p-4 font-mono text-xs leading-relaxed outline-none focus-visible:ring-2 focus-visible:ring-ring/50"
    >
      {logs.map((entry) => (
        <div key={entry.id} className="flex gap-3">
          {/* The first entries are stamped during SSR, a moment before hydration. */}
          <span suppressHydrationWarning className="shrink-0 text-muted-foreground tabular-nums">
            {fmtTs(entry.ts)}
          </span>
          <span className={`w-9 shrink-0 ${LEVEL_CLASS[entry.level]}`}>{entry.level}</span>
          <span className="min-w-0 break-words">{entry.message}</span>
        </div>
      ))}
    </div>
  );
}
