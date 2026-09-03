"use client";

import { useEffect, useRef } from "react";
import styles from "./BackendCard.module.css";
import type { BackendStat } from "@/types/stats";
import { BACKEND_COLORS } from "@/lib/constants";

interface Props {
  backend: BackendStat;
  index: number;
  maxConn: number;
  totalConn: number;
  prevConnections?: number;
}

export default function BackendCard({ backend, index, maxConn, totalConn, prevConnections }: Props) {
  const colors = BACKEND_COLORS[index % BACKEND_COLORS.length];
  const state = backend.healthy ? "healthy" : "unhealthy";
  const fillPct = maxConn > 0 ? (backend.connections / maxConn) * 100 : 0;
  const sharePct = totalConn > 0 ? ((backend.connections / totalConn) * 100).toFixed(1) : "0.0";

  const packetRef = useRef<HTMLDivElement>(null);
  const prevRef = useRef<number | undefined>(prevConnections);

  useEffect(() => {
    if (prevRef.current !== undefined && backend.connections > prevRef.current && packetRef.current) {
      // Spawn a packet animation
      const packet = document.createElement("div");
      packet.className = styles.packet;
      packet.style.background = colors.from;
      packetRef.current.appendChild(packet);
      setTimeout(() => packet.remove(), 900);
    }
    prevRef.current = backend.connections;
  }, [backend.connections, colors.from]);

  const icons = ["🖥️", "💻", "🗄️", "🔧"];

  return (
    <div className={`${styles.card} ${styles[state]}`} ref={packetRef}>
      <div className={`${styles.icon} ${styles[state]}`}>{icons[index % icons.length]}</div>

      <div className={styles.info}>
        <div className={styles.name}>
          <span>:{backend.port}</span>
          <span
            className={`${styles.badge} ${backend.healthy ? styles.badgeHealthy : styles.badgeUnhealthy}`}
          >
            {state.toUpperCase()}
          </span>
        </div>
        <div className={styles.barRow}>
          <div className={styles.barTrack}>
            <div
              className={styles.barFill}
              style={{
                width: `${fillPct}%`,
                background: `linear-gradient(90deg, ${colors.from}, ${colors.to})`,
              }}
            />
          </div>
          <span className={styles.shareLabel}>{sharePct}% share</span>
        </div>
      </div>

      <div className={styles.connBlock}>
        <div className={styles.connCount} style={{ color: colors.text }}>
          {backend.connections}
        </div>
        <div className={styles.connLabel}>connections</div>
      </div>
    </div>
  );
}
