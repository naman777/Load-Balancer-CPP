"use client";

import { useEffect, useRef } from "react";
import styles from "./EventLog.module.css";
import type { LogEntry } from "@/types/stats";

interface Props {
  logs: LogEntry[];
}

function fmtTs(d: Date) {
  const p = (n: number) => String(n).padStart(2, "0");
  return `[${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}]`;
}

export default function EventLog({ logs }: Props) {
  const bodyRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const el = bodyRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [logs]);

  return (
    <div className={styles.terminal}>
      <div className={styles.topbar}>
        <div className={styles.dot} style={{ background: "#f87171" }} />
        <div className={styles.dot} style={{ background: "#fbbf24" }} />
        <div className={styles.dot} style={{ background: "#34d399" }} />
        <span className={styles.title}>load_balancer dashboard log</span>
      </div>
      <div className={styles.body} ref={bodyRef}>
        {logs.map((entry) => (
          <span key={entry.id} className={styles.line}>
            <span className={styles.ts}>{fmtTs(entry.ts)} </span>
            <span className={styles[entry.level.toLowerCase() as "info" | "warn" | "err"]}>
              {entry.level.padEnd(5)}{" "}
            </span>
            <span className={styles.msg}>{entry.message}</span>
          </span>
        ))}
      </div>
    </div>
  );
}
