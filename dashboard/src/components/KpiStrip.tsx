"use client";

import styles from "./KpiStrip.module.css";
import type { StatsResponse } from "@/types/stats";
import type { Algorithm } from "@/types/stats";
import { ALGO_META } from "@/lib/constants";

interface Props {
  data: StatsResponse | undefined;
  error: unknown;
  pollCount: number;
  algo: Algorithm;
}

export default function KpiStrip({ data, error, pollCount, algo }: Props) {
  const healthy = data?.backends.filter((b) => b.healthy).length ?? 0;
  const total = data?.backends.length ?? 0;
  const totalConn = data?.backends.reduce((s, b) => s + b.connections, 0) ?? 0;
  const algoMeta = ALGO_META.find((a) => a.key === algo);

  const kpis = [
    {
      label: "Listen Port",
      value: data?.port ?? "—",
      sub: "TCP/HTTP frontend",
      accent: "var(--cyan)",
    },
    {
      label: "Healthy Backends",
      value: error ? "✕" : data ? `${healthy}` : "—",
      sub: data ? `of ${total} total` : "waiting…",
      accent: error ? "var(--red)" : "var(--green)",
    },
    {
      label: "Active Connections",
      value: data ? totalConn : "—",
      sub: "across all backends",
      accent: "var(--accent2)",
    },
    {
      label: "Algorithm",
      value: algo.toUpperCase(),
      sub: algoMeta?.shortLabel ? algoMeta.label : "—",
      accent: "var(--orange)",
      small: true,
    },
    {
      label: "Polls Done",
      value: pollCount,
      sub: "every 2 seconds",
      accent: "var(--yellow)",
    },
  ];

  return (
    <div className={styles.strip}>
      {kpis.map((kpi) => (
        <div key={kpi.label} className={styles.card} style={{ "--accent-color": kpi.accent } as React.CSSProperties}>
          <span className={styles.label}>{kpi.label}</span>
          <span className={styles.value} style={{ fontSize: kpi.small ? "1.3rem" : undefined }}>
            {kpi.value}
          </span>
          <span className={styles.sub}>{kpi.sub}</span>
        </div>
      ))}
    </div>
  );
}
