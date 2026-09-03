"use client";

import styles from "./DistributionBar.module.css";
import type { BackendStat } from "@/types/stats";
import { BACKEND_COLORS } from "@/lib/constants";

interface Props {
  backends: BackendStat[];
}

export default function DistributionBar({ backends }: Props) {
  const total = backends.reduce((s, b) => s + b.connections, 0);

  if (total === 0) {
    return (
      <div>
        <div className={styles.bar}>
          <div className={styles.idle}>idle — no active connections</div>
        </div>
      </div>
    );
  }

  return (
    <div>
      <div className={styles.bar}>
        {backends.map((b, i) => {
          const pct = (b.connections / total) * 100;
          const colors = BACKEND_COLORS[i % BACKEND_COLORS.length];
          return (
            <div
              key={b.port}
              className={styles.seg}
              style={{
                width: `${pct}%`,
                background: colors.from,
                opacity: b.healthy ? 1 : 0.3,
              }}
              title={`:${b.port} — ${pct.toFixed(1)}%`}
            >
              {pct > 8 ? `${pct.toFixed(0)}%` : ""}
            </div>
          );
        })}
      </div>
      <div className={styles.legend}>
        {backends.map((b, i) => {
          const colors = BACKEND_COLORS[i % BACKEND_COLORS.length];
          const pct = ((b.connections / total) * 100).toFixed(1);
          return (
            <span key={b.port} className={styles.legendItem}>
              <span className={styles.dot} style={{ background: colors.from }} />
              <span>:{b.port}</span>
              <span className={styles.pct}>{pct}%</span>
            </span>
          );
        })}
      </div>
    </div>
  );
}
