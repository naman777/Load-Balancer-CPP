"use client";

import { useState } from "react";
import styles from "./page.module.css";
import { useStats } from "@/hooks/useStats";
import type { Algorithm } from "@/types/stats";

import KpiStrip from "@/components/KpiStrip";
import BackendCard from "@/components/BackendCard";
import AlgoSelector from "@/components/AlgoSelector";
import ConnectionChart from "@/components/ConnectionChart";
import DistributionBar from "@/components/DistributionBar";
import EventLog from "@/components/EventLog";
import JsonPanel from "@/components/JsonPanel";
import LoadTestPanel from "@/components/LoadTestPanel";

const FEATURES = [
  { icon: "🔀", title: "4 Algorithms", desc: "LC · RR · IP-hash · Rendezvous HRW" },
  { icon: "⚖️", title: "Weighted Backends", desc: "Per-backend traffic ratios" },
  { icon: "❤️", title: "Health Checks", desc: "HTTP HEAD every 5 seconds" },
  { icon: "🧵", title: "Thread Pool", desc: "Fixed 16 workers, no unbounded spawning" },
  { icon: "🔒", title: "Connection Cap", desc: "Hard limit per backend — skip at capacity" },
  { icon: "🔁", title: "SIGHUP Reload", desc: "Live config — zero downtime" },
  { icon: "📊", title: "Stats Endpoint", desc: "JSON on :8081/stats" },
  { icon: "⚡", title: "No Libraries", desc: "Pure C++17 · POSIX sockets only" },
];

const ARCH_THREADS = [
  { color: "var(--cyan)",    label: "Main thread",        desc: "accept() loop :8080" },
  { color: "var(--accent2)", label: "Worker pool (16)",   desc: "handle_client() × N" },
  { color: "var(--green)",   label: "Health check",       desc: "HTTP HEAD every 5 s" },
  { color: "var(--orange)",  label: "Stats thread",       desc: "JSON on :8081" },
  { color: "var(--yellow)",  label: "SIGHUP thread",      desc: "sigwait() reload" },
];

const ARCH_DECISIONS = [
  { color: "var(--green)",   label: "TOCTOU-free",        desc: "Single mutex for select + increment" },
  { color: "var(--cyan)",    label: "atomic<Algorithm>",  desc: "Lock-free reads in worker threads" },
  { color: "var(--accent2)", label: "SO_REUSEADDR",       desc: "Instant restart after crash" },
  { color: "var(--orange)",  label: "Retry on failure",   desc: "Next healthy backend on connect fail" },
  { color: "var(--yellow)",  label: "sigwait()",          desc: "Safe async signal handling" },
];

export default function Home() {
  const [endpoint, setEndpoint] = useState(
    process.env.NEXT_PUBLIC_STATS_URL || "/api/stats"
  );
  const [algo, setAlgo] = useState<Algorithm>("lc");

  // LB frontend URL — used by the load test panel to fire requests
  const lbUrl = process.env.NEXT_PUBLIC_LB_URL ||
    "http://13.206.180.74:8080";

  const { data, error, isLoading, logs, connHistory, pollCount } = useStats(endpoint);

  const backends = data?.backends ?? [];
  const maxConn = Math.max(...backends.map((b) => b.connections), 1);
  const totalConn = backends.reduce((s, b) => s + b.connections, 0);

  const isConnected = !!data && !error;
  const isDisconnected = !!error;

  return (
    <main className={styles.main}>
      <div className={styles.wrapper}>

        {/* ── Header ────────────────────────────────────────────── */}
        <header className={styles.header}>
          <div className={styles.logoArea}>
            <div className={styles.logoIcon}>⚖️</div>
            <div className={styles.logoText}>
              <h1 className={styles.logoTitle}>
                C++ Load Balancer
                <span className={styles.badge}>C++17</span>
              </h1>
              <p className={styles.logoSub}>
                Production-quality TCP/HTTP load balancer — built from scratch, no libraries
              </p>
            </div>
          </div>

          <div className={styles.headerRight}>
            <input
              id="endpoint-input"
              className={styles.endpointInput}
              value={endpoint}
              onChange={(e) => setEndpoint(e.target.value)}
              placeholder="/api/stats"
              title="Stats endpoint base URL"
            />
            <div
              className={`${styles.statusPill} ${
                isConnected ? styles.connected :
                isDisconnected ? styles.disconnected :
                styles.connecting
              }`}
            >
              <span className={`${styles.dot} ${isLoading || (!data && !error) ? styles.pulse : ""}`} />
              <span>
                {isConnected ? "● Connected" : isDisconnected ? "✕ Disconnected" : "Connecting…"}
              </span>
            </div>
          </div>
        </header>

        {/* ── KPI Strip ────────────────────────────────────────── */}
        <KpiStrip data={data} error={error} pollCount={pollCount} algo={algo} />

        {/* ── Main Grid ─────────────────────────────────────────── */}
        <div className={styles.mainGrid}>

          {/* Left column */}
          <div className={styles.leftCol}>
            <div className={styles.sectionTitle}>Backend Servers</div>
            <div className={styles.backendsGrid}>
              {backends.length === 0 ? (
                <div className={styles.emptyState}>
                  {error
                    ? "⚠ Cannot reach stats endpoint — start the load balancer with ./scripts/start.sh"
                    : "Waiting for data… start the load balancer first."}
                </div>
              ) : (
                backends.map((b, i) => (
                  <BackendCard
                    key={b.port}
                    backend={b}
                    index={i}
                    maxConn={maxConn}
                    totalConn={totalConn}
                  />
                ))
              )}
            </div>

            {/* Distribution bar */}
            <div style={{ marginTop: 20 }}>
              <div className={styles.sectionTitle}>Connection Distribution</div>
              <div className="card" style={{ padding: "16px 20px" }}>
                <div style={{ fontSize: "0.75rem", color: "var(--muted)", marginBottom: 4 }}>
                  Traffic split across all backends
                </div>
                {backends.length > 0 ? (
                  <DistributionBar backends={backends} />
                ) : (
                  <div style={{ height: 28, background: "var(--dim)", borderRadius: "var(--radius-sm)", marginTop: 10 }} />
                )}
              </div>
            </div>

            {/* Connection chart */}
            <div style={{ marginTop: 20 }}>
              <div className={styles.sectionTitle}>Total Connections Over Time</div>
              <div className="card" style={{ padding: "16px 20px" }}>
                <ConnectionChart history={connHistory} />
                <div className={styles.refreshBar}>
                  <div className={styles.refreshFill} />
                </div>
              </div>
            </div>
          </div>

          {/* Right column */}
          <div className={styles.rightCol}>
            <div className="card">
              <div className={styles.sectionTitle}>Algorithm</div>
              <AlgoSelector value={algo} onChange={setAlgo} />
              <p className={styles.algoNote}>
                ℹ Apply via: <code>kill -HUP $(pgrep load_balancer)</code> after editing <code>lb.conf</code>
              </p>
            </div>

            <div className="card">
              <div className={styles.sectionTitle}>Live Stats JSON</div>
              <JsonPanel data={data} error={error} />
            </div>

            <div className="card">
              <div className={styles.sectionTitle}>Configuration</div>
              <table className={styles.configTable}>
                <tbody>
                  <tr><td>Port</td><td>{data?.port ?? "—"}</td></tr>
                  <tr><td>Stats Port</td><td>{data ? data.port + 1 : "—"}</td></tr>
                  <tr><td>Algorithm</td><td>{algo} — {algo === "lc" ? "Least-Connections" : algo === "rr" ? "Round-Robin" : algo === "ih" ? "IP Hash" : "Rendezvous HRW"}</td></tr>
                  <tr><td>Thread Pool</td><td>16 workers</td></tr>
                  <tr><td>Health Check</td><td>every 5 s</td></tr>
                  <tr><td>Connection: close</td><td>injected ✓</td></tr>
                  <tr><td>SIGHUP reload</td><td>algo + weights</td></tr>
                </tbody>
              </table>
            </div>
          </div>
        </div>

        {/* ── Load Test ─────────────────────────────────────────── */}
        <div style={{ marginBottom: 20 }}>
          <div className={styles.sectionTitle}>⚡ Live Load Test</div>
          <div className="card">
            <p className={styles.loadTestIntro}>
              Fire concurrent HTTP requests directly to the load balancer and watch how
              it distributes traffic across backends in real time. Connection counts above
              animate as the requests hit.
            </p>
            <LoadTestPanel lbUrl={lbUrl} />
          </div>
        </div>

        {/* ── Bottom Grid ────────────────────────────────────────── */}
        <div className={styles.bottomGrid}>
          <div>
            <div className={styles.sectionTitle}>Event Log</div>
            <EventLog logs={logs} />
          </div>

          <div>
            <div className={styles.sectionTitle}>Architecture</div>
            <div className="card" style={{ padding: 16 }}>
              <div className={styles.archBlock}>
                <div className={styles.archGroup}>
                  <div className={styles.archGroupTitle}>Thread Model (single process)</div>
                  {ARCH_THREADS.map((t) => (
                    <div key={t.label} className={styles.archRow}>
                      <span style={{ color: t.color }}>• {t.label}</span>
                      <span className={styles.archDesc}>{t.desc}</span>
                    </div>
                  ))}
                </div>
                <div className={styles.archGroup}>
                  <div className={styles.archGroupTitle}>Key Design Decisions</div>
                  {ARCH_DECISIONS.map((d) => (
                    <div key={d.label} className={styles.archRow}>
                      <span style={{ color: d.color }}>• {d.label}</span>
                      <span className={styles.archDesc}>{d.desc}</span>
                    </div>
                  ))}
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* ── Feature Chips ─────────────────────────────────────── */}
        <div className={styles.featuresRow}>
          {FEATURES.map((f) => (
            <div key={f.title} className={styles.featureChip}>
              <span className={styles.featureIcon}>{f.icon}</span>
              <div>
                <div className={styles.featureTitle}>{f.title}</div>
                <div className={styles.featureDesc}>{f.desc}</div>
              </div>
            </div>
          ))}
        </div>

      </div>
    </main>
  );
}
