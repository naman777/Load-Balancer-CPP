"use client";

import type { BackendStat } from "@/types/stats";
import { seriesColor } from "@/lib/constants";

interface Props {
  backends: BackendStat[];
}

export default function TrafficSplit({ backends }: Props) {
  const total = backends.reduce((s, b) => s + b.connections, 0);
  const series = backends.map((b, i) => ({
    ...b,
    color: seriesColor(i),
    pct: total > 0 ? (b.connections / total) * 100 : 0,
  }));

  return (
    <div>
      {total === 0 ? (
        <div className="h-5 rounded bg-black/[0.08] dark:bg-white/10" />
      ) : (
        <div className="flex h-5 gap-0.5">
          {series
            .filter((s) => s.connections > 0)
            .map((s) => (
              <div
                key={s.port}
                tabIndex={0}
                aria-label={`:${s.port}, ${s.connections} connections, ${s.pct.toFixed(1)}%`}
                className="group/seg relative h-full min-w-1 outline-none transition-[width] duration-500 ease-out first:rounded-l last:rounded-r hover:brightness-110 focus-visible:ring-2 focus-visible:ring-ring"
                style={{ width: `${s.pct}%`, background: s.color, opacity: s.healthy ? 1 : 0.35 }}
              >
                <div
                  role="tooltip"
                  className="pointer-events-none absolute bottom-full left-1/2 z-10 mb-2 hidden -translate-x-1/2 rounded-md border border-card-edge-hover bg-popover px-2.5 py-1.5 text-xs whitespace-nowrap text-popover-foreground group-hover/seg:block group-focus-visible/seg:block"
                >
                  <span className="font-semibold">{s.connections}</span>{" "}
                  <span className="text-muted-foreground">
                    on :{s.port} ({s.pct.toFixed(1)}%)
                  </span>
                </div>
              </div>
            ))}
        </div>
      )}

      <ul className="mt-4 flex flex-wrap gap-x-5 gap-y-2 text-xs">
        {series.map((s) => (
          <li key={s.port} className="flex items-center gap-1.5">
            <span aria-hidden="true" className="size-2.5 rounded-[3px]" style={{ background: s.color }} />
            <span className="font-mono text-muted-foreground">:{s.port}</span>
            <span className="font-medium tabular-nums">{s.pct.toFixed(1)}%</span>
            {!s.healthy && <span className="text-red-700 dark:text-red-400">down</span>}
          </li>
        ))}
      </ul>

      {total === 0 && (
        <p className="mt-3 text-xs text-muted-foreground">Idle, no active connections right now.</p>
      )}
    </div>
  );
}
