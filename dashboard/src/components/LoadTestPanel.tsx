"use client";

import { useState, useRef } from "react";
import styles from "./LoadTestPanel.module.css";
import type { BlastResult } from "@/app/api/blast/route";

interface Summary {
  total: number;
  success: number;
  failed: number;
  avgMs: number;
  minMs: number;
  maxMs: number;
}

interface Props {
  lbUrl: string;
  onFired?: () => void; // callback so parent can trigger a stats refresh
}

export default function LoadTestPanel({ lbUrl, onFired }: Props) {
  const [count, setCount] = useState(20);
  const [url, setUrl] = useState(lbUrl);
  const [firing, setFiring] = useState(false);
  const [results, setResults] = useState<BlastResult[]>([]);
  const [summary, setSummary] = useState<Summary | null>(null);
  const [phase, setPhase] = useState<"idle" | "firing" | "done">("idle");
  const listRef = useRef<HTMLDivElement>(null);

  async function fire() {
    setFiring(true);
    setPhase("firing");
    setResults([]);
    setSummary(null);

    try {
      const res = await fetch("/api/blast", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ count, url }),
      });
      const data = await res.json();
      // Animate results in one by one for visual effect
      const all: BlastResult[] = data.results;
      setSummary(data.summary);

      for (let i = 0; i < all.length; i++) {
        await new Promise<void>((r) => setTimeout(r, 30));
        setResults((prev) => [...prev, all[i]]);
        if (listRef.current) listRef.current.scrollTop = listRef.current.scrollHeight;
      }

      setPhase("done");
      onFired?.();
    } catch (err) {
      console.error(err);
      setPhase("done");
    } finally {
      setFiring(false);
    }
  }

  const successPct = summary ? Math.round((summary.success / summary.total) * 100) : 0;

  return (
    <div className={styles.panel}>
      {/* Controls */}
      <div className={styles.controls}>
        <div className={styles.controlGroup}>
          <label className={styles.controlLabel}>
            Target URL <span className={styles.urlHint}>(LB frontend port 8080)</span>
          </label>
          <input
            id="lb-target-url"
            className={styles.urlInput}
            value={url}
            onChange={(e) => setUrl(e.target.value)}
            placeholder="http://13.206.180.74:8080"
            disabled={firing}
          />
        </div>

        <div className={styles.sliderRow}>
          <div className={styles.controlGroup} style={{ flex: 1 }}>
            <label className={styles.controlLabel}>
              Concurrent Requests
              <span className={styles.countBadge}>{count}</span>
            </label>
            <input
              id="request-count-slider"
              type="range"
              min={1}
              max={100}
              value={count}
              onChange={(e) => setCount(Number(e.target.value))}
              className={styles.slider}
              disabled={firing}
            />
            <div className={styles.sliderTicks}>
              <span>1</span><span>25</span><span>50</span><span>75</span><span>100</span>
            </div>
          </div>

          <button
            id="fire-requests-btn"
            className={`${styles.fireBtn} ${firing ? styles.fireBtnActive : ""}`}
            onClick={fire}
            disabled={firing}
            type="button"
          >
            {firing ? (
              <>
                <span className={styles.spinner} />
                Firing…
              </>
            ) : (
              <>🚀 Fire {count} Requests</>
            )}
          </button>
        </div>
      </div>

      {/* Burst animation */}
      {phase === "firing" && (
        <div className={styles.burstBar}>
          {Array.from({ length: count }).map((_, i) => (
            <div
              key={i}
              className={styles.burstDot}
              style={{ animationDelay: `${(i * 40) % 600}ms` }}
            />
          ))}
        </div>
      )}

      {/* Summary strip */}
      {summary && (
        <div className={styles.summaryStrip}>
          <div className={styles.summaryCard} style={{ "--c": "var(--green)" } as React.CSSProperties}>
            <div className={styles.summaryVal}>{summary.success}</div>
            <div className={styles.summaryLbl}>Success</div>
          </div>
          <div className={styles.summaryCard} style={{ "--c": summary.failed > 0 ? "var(--red)" : "var(--muted)" } as React.CSSProperties}>
            <div className={styles.summaryVal}>{summary.failed}</div>
            <div className={styles.summaryLbl}>Failed</div>
          </div>
          <div className={styles.summaryCard} style={{ "--c": "var(--cyan)" } as React.CSSProperties}>
            <div className={styles.summaryVal}>{summary.avgMs}ms</div>
            <div className={styles.summaryLbl}>Avg Latency</div>
          </div>
          <div className={styles.summaryCard} style={{ "--c": "var(--accent2)" } as React.CSSProperties}>
            <div className={styles.summaryVal}>{summary.minMs}ms</div>
            <div className={styles.summaryLbl}>Min</div>
          </div>
          <div className={styles.summaryCard} style={{ "--c": "var(--orange)" } as React.CSSProperties}>
            <div className={styles.summaryVal}>{summary.maxMs}ms</div>
            <div className={styles.summaryLbl}>Max</div>
          </div>
          <div className={styles.summaryCard} style={{ "--c": successPct === 100 ? "var(--green)" : "var(--yellow)" } as React.CSSProperties}>
            <div className={styles.summaryVal}>{successPct}%</div>
            <div className={styles.summaryLbl}>Success Rate</div>
          </div>
        </div>
      )}

      {/* Success bar */}
      {summary && (
        <div className={styles.successBarTrack}>
          <div
            className={styles.successBarFill}
            style={{ width: `${successPct}%` }}
          />
        </div>
      )}

      {/* Results list */}
      {results.length > 0 && (
        <div className={styles.resultsList} ref={listRef}>
          <div className={styles.resultsHeader}>
            <span className={styles.colId}>#</span>
            <span className={styles.colStatus}>Status</span>
            <span className={styles.colMs}>Time</span>
            <span className={styles.colBody}>Response</span>
          </div>
          {results.map((r) => (
            <div
              key={r.id}
              className={`${styles.resultRow} ${r.ok ? styles.resultOk : styles.resultErr}`}
            >
              <span className={styles.colId}>{String(r.id).padStart(2, "0")}</span>
              <span className={`${styles.colStatus} ${r.ok ? styles.statusOk : styles.statusErr}`}>
                {r.status > 0 ? r.status : "ERR"}
              </span>
              <span className={styles.colMs}>
                <span className={styles.msBar} style={{ width: `${Math.min((r.ms / 2000) * 80, 80)}px` }} />
                {r.ms}ms
              </span>
              <span className={styles.colBody}>{r.error ?? r.body}</span>
            </div>
          ))}
        </div>
      )}

      {phase === "idle" && results.length === 0 && (
        <div className={styles.emptyHint}>
          <div className={styles.emptyIcon}>⚡</div>
          <div>Fire requests to see them distributed across backends</div>
          <div className={styles.emptySubHint}>Watch the backend connection counts animate above in real time</div>
        </div>
      )}
    </div>
  );
}
