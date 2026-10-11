"use client";

import { useEffect, useRef, useState } from "react";

interface Props {
  history: number[];
  /** Seconds between samples. */
  intervalSec?: number;
}

const H = 180;
const M = { top: 12, right: 12, bottom: 22, left: 34 };

// Smallest of 2, 4, 10, 20, 40, 100... that covers the data, so the three
// ticks (0, half, max) are always whole numbers.
function niceMax(max: number) {
  for (let pow = 1; ; pow *= 10) {
    for (const m of [2, 4, 10]) {
      if (m * pow >= max) return m * pow;
    }
  }
}

function ago(stepsBack: number, intervalSec: number) {
  return stepsBack === 0 ? "now" : `${stepsBack * intervalSec}s ago`;
}

export default function ConnectionChart({ history, intervalSec = 2 }: Props) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(0);
  const [active, setActive] = useState<number | null>(null);

  useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    const ro = new ResizeObserver(([entry]) => setWidth(entry.contentRect.width));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const n = history.length;
  const ready = n >= 2 && width > 0;
  const top = niceMax(Math.max(...history, 1));
  const plotW = Math.max(width - M.left - M.right, 0);
  const plotH = H - M.top - M.bottom;
  const x = (i: number) => M.left + (n > 1 ? (i / (n - 1)) * plotW : 0);
  const y = (v: number) => M.top + plotH - (v / top) * plotH;

  const line = history.map((v, i) => `${i === 0 ? "M" : "L"}${x(i).toFixed(1)},${y(v).toFixed(1)}`).join(" ");
  const area = ready ? `${line} L${x(n - 1).toFixed(1)},${y(0)} L${x(0).toFixed(1)},${y(0)} Z` : "";
  const idx = active !== null ? Math.min(active, n - 1) : null;

  function nearest(clientX: number) {
    const rect = wrapRef.current!.getBoundingClientRect();
    const ratio = (clientX - rect.left - M.left) / plotW;
    return Math.round(Math.min(Math.max(ratio, 0), 1) * (n - 1));
  }

  function onKeyDown(e: React.KeyboardEvent) {
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    e.preventDefault();
    const from = idx ?? n - 1;
    setActive(Math.min(Math.max(from + (e.key === "ArrowLeft" ? -1 : 1), 0), n - 1));
  }

  return (
    <div ref={wrapRef} className="relative w-full" style={{ height: H }}>
      {!ready ? (
        <div className="flex h-full items-center justify-center text-sm text-muted-foreground">
          Collecting data…
        </div>
      ) : (
        <>
          <svg
            width={width}
            height={H}
            role="img"
            tabIndex={0}
            aria-label={`Total active connections over the last ${(n - 1) * intervalSec} seconds. Latest value ${history[n - 1]}, peak ${Math.max(...history)}. Use the arrow keys to read each sample.`}
            className="block rounded outline-none focus-visible:ring-2 focus-visible:ring-ring/50"
            onPointerMove={(e) => setActive(nearest(e.clientX))}
            onPointerLeave={() => setActive(null)}
            onFocus={() => setActive((a) => a ?? n - 1)}
            onBlur={() => setActive(null)}
            onKeyDown={onKeyDown}
          >
            {[0, top / 2, top].map((t) => (
              <g key={t}>
                <line x1={M.left} x2={width - M.right} y1={y(t)} y2={y(t)} className="stroke-border" strokeWidth={1} />
                <text x={M.left - 8} y={y(t) + 3.5} textAnchor="end" className="fill-muted-foreground text-[11px] tabular-nums">
                  {t}
                </text>
              </g>
            ))}
            <text x={M.left} y={H - 4} className="fill-muted-foreground text-[11px]">
              {ago(n - 1, intervalSec)}
            </text>
            <text x={width - M.right} y={H - 4} textAnchor="end" className="fill-muted-foreground text-[11px]">
              now
            </text>

            <path d={area} className="fill-foreground/10" />
            <path d={line} fill="none" className="stroke-foreground" strokeWidth={2} strokeLinejoin="round" strokeLinecap="round" />

            {idx !== null && (
              <line x1={x(idx)} x2={x(idx)} y1={M.top} y2={y(0)} className="stroke-muted-foreground" strokeWidth={1} />
            )}
            {/* The latest point, or the one under the crosshair, with a surface ring. */}
            <circle
              cx={x(idx ?? n - 1)}
              cy={y(history[idx ?? n - 1])}
              r={4}
              className="fill-foreground stroke-background"
              strokeWidth={2}
            />
          </svg>

          {idx !== null && (
            <div
              className="pointer-events-none absolute top-0 z-10 rounded-md border border-card-edge-hover bg-popover px-2.5 py-1.5 text-xs whitespace-nowrap text-popover-foreground"
              style={{
                left: x(idx),
                transform: `translateX(${x(idx) > width / 2 ? "calc(-100% - 10px)" : "10px"})`,
              }}
            >
              <span className="font-semibold tabular-nums">{history[idx]}</span>{" "}
              <span className="text-muted-foreground">
                {history[idx] === 1 ? "connection" : "connections"}, {ago(n - 1 - idx, intervalSec)}
              </span>
            </div>
          )}
        </>
      )}
    </div>
  );
}
