"use client";

import useSWR from "swr";
import { useCallback, useRef, useState } from "react";
import type { StatsResponse, LogEntry } from "@/types/stats";

let logIdCounter = 0;

const fetcher = async (url: string): Promise<StatsResponse> => {
  const res = await fetch(url, { signal: AbortSignal.timeout(3000) });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
};

export function useStats(endpoint: string) {
  const normalizedEndpoint = endpoint.replace(/\/$/, "");
  const url = normalizedEndpoint.endsWith("/stats")
    ? normalizedEndpoint
    : `${normalizedEndpoint}/stats`;
  const prevRef = useRef<StatsResponse | null>(null);
  const [logs, setLogs] = useState<LogEntry[]>([
    { id: logIdCounter++, ts: new Date(), level: "INFO", message: "Dashboard started. Connecting to stats endpoint…" },
    { id: logIdCounter++, ts: new Date(), level: "INFO", message: `Polling: ${url}` },
    { id: logIdCounter++, ts: new Date(), level: "INFO", message: "Start LB: ./scripts/start.sh" },
    { id: logIdCounter++, ts: new Date(), level: "INFO", message: "Test:     curl http://13.206.180.74:8080/" },
  ]);
  const [connHistory, setConnHistory] = useState<number[]>([]);
  const [pollCount, setPollCount] = useState(0);

  const addLog = useCallback((level: LogEntry["level"], message: string) => {
    setLogs((prev) => {
      const next = [...prev, { id: logIdCounter++, ts: new Date(), level, message }];
      return next.slice(-100);
    });
  }, []);

  const { data, error, isLoading } = useSWR<StatsResponse>(url, fetcher, {
    refreshInterval: 2000,
    onSuccess: (data) => {
      setPollCount((c) => c + 1);
      const total = data.backends.reduce((s, b) => s + b.connections, 0);
      setConnHistory((h) => [...h.slice(-39), total]);

      const prev = prevRef.current;
      if (!prev) {
        const healthy = data.backends.filter((b) => b.healthy).length;
        addLog("INFO", `Connected — ${data.backends.length} backends, ${healthy} healthy, ${total} active conns`);
      } else {
        prev.backends.forEach((pb, i) => {
          const nb = data.backends[i];
          if (!nb) return;
          if (pb.healthy && !nb.healthy) addLog("WARN", `Backend :${nb.port} is DOWN — removed from rotation`);
          else if (!pb.healthy && nb.healthy) addLog("INFO", `Backend :${nb.port} is UP — added back to rotation`);
          const delta = nb.connections - pb.connections;
          if (delta !== 0)
            addLog("INFO", `Backend :${nb.port} conns ${delta > 0 ? "+" : ""}${delta} → ${nb.connections}`);
        });
      }
      prevRef.current = data;
    },
    onError: (err) => {
      addLog("ERR", `Cannot reach ${url} — ${err.message}`);
    },
  });

  return { data, error, isLoading, logs, connHistory, pollCount, addLog };
}
