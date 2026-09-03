"use client";

import { useEffect, useRef } from "react";
import styles from "./ConnectionChart.module.css";

interface Props {
  history: number[];
}

export default function ConnectionChart({ history }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    const rect = canvas.parentElement!.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = 120 * dpr;
    canvas.style.width = `${rect.width}px`;
    canvas.style.height = "120px";
    ctx.scale(dpr, dpr);

    const W = rect.width;
    const H = 120;

    ctx.clearRect(0, 0, W, H);

    if (history.length < 2) {
      ctx.fillStyle = "rgba(100,116,139,0.4)";
      ctx.font = "12px JetBrains Mono, monospace";
      ctx.textAlign = "center";
      ctx.fillText("Collecting data…", W / 2, H / 2);
      return;
    }

    const max = Math.max(...history, 1);
    const pts = history.map((v, i) => ({
      x: (i / (history.length - 1)) * W,
      y: H - 10 - ((v / max) * (H - 20)),
    }));

    // Grid lines
    ctx.strokeStyle = "rgba(99,120,220,0.08)";
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = 10 + (i * (H - 20)) / 4;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(W, y);
      ctx.stroke();
    }

    // Y-axis labels
    ctx.fillStyle = "rgba(100,116,139,0.6)";
    ctx.font = "9px JetBrains Mono, monospace";
    ctx.textAlign = "right";
    for (let i = 0; i <= 4; i++) {
      const y = 10 + (i * (H - 20)) / 4;
      const val = Math.round(max - (i / 4) * max);
      ctx.fillText(String(val), W - 4, y + 3);
    }

    // Fill gradient
    const grad = ctx.createLinearGradient(0, 0, 0, H);
    grad.addColorStop(0, "rgba(99,120,220,0.35)");
    grad.addColorStop(1, "rgba(99,120,220,0.0)");
    ctx.beginPath();
    ctx.moveTo(pts[0].x, H);
    pts.forEach((p) => ctx.lineTo(p.x, p.y));
    ctx.lineTo(pts[pts.length - 1].x, H);
    ctx.closePath();
    ctx.fillStyle = grad;
    ctx.fill();

    // Line
    ctx.beginPath();
    pts.forEach((p, i) => (i === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y)));
    ctx.strokeStyle = "rgba(99,120,220,0.9)";
    ctx.lineWidth = 2;
    ctx.lineJoin = "round";
    ctx.stroke();

    // Latest dot
    const last = pts[pts.length - 1];
    ctx.beginPath();
    ctx.arc(last.x, last.y, 4, 0, Math.PI * 2);
    ctx.fillStyle = "#6378dc";
    ctx.fill();
    ctx.strokeStyle = "#fff";
    ctx.lineWidth = 1.5;
    ctx.stroke();
  }, [history]);

  return (
    <div className={styles.wrapper}>
      <canvas ref={canvasRef} className={styles.canvas} />
    </div>
  );
}
