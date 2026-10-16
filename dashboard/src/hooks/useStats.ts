"use client";

import useSWR from "swr";
import { useCallback, useEffect, useRef, useState } from "react";
import type { StatsResponse, LogEntry } from "@/types/stats";
import { DEFAULT_LB_URL } from "@/lib/backend";

let logIdCounter = 0;

/** Seconds between points on the connection chart. */
export const HISTORY_INTERVAL_SEC = 1;

const fetcher = async (url: string): Promise<StatsResponse> => {
  const res = await fetch(url, { signal: AbortSignal.timeout(3000) });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
};

const totalConnections = (s: StatsResponse) => s.backends.reduce((n, b) => n + b.connections, 0);

// Live stats from the load balancer. Its stats server pushes a snapshot over
// Server-Sent Events (GET /stats/stream) whenever the numbers change, at most
// 10 times a second. While the stream is down or unreachable this polls
// `endpoint` every 2 s instead, and EventSource keeps reconnecting on its own.
export function useStats(endpoint: string, streamUrl: string | null) {
  const normalizedEndpoint = endpoint.replace(/\/$/, "");
  const url = normalizedEndpoint.endsWith("/stats")
    ? normalizedEndpoint
    : `${normalizedEndpoint}/stats`;

  const [data, setData] = useState<StatsResponse>();
  const latestRef = useRef<StatsResponse | null>(null);
  const sampledRef = useRef<StatsResponse | null>(null);
  // The stream URL that is currently delivering events, so a new URL starts out not live.
  const [liveUrl, setLiveUrl] = useState<string | null>(null);
  const live = !!streamUrl && liveUrl === streamUrl;

  const [logs, setLogs] = useState<LogEntry[]>([
    { id: logIdCounter++, ts: new Date(), level: "INFO", message: "Dashboard started. Connecting to stats endpoint…" },
    { id: logIdCounter++, ts: new Date(), level: "INFO", message: `Live stream: ${streamUrl ?? "off"} (falls back to polling ${url})` },
    { id: logIdCounter++, ts: new Date(), level: "INFO", message: "Start LB: ./scripts/start.sh" },
    { id: logIdCounter++, ts: new Date(), level: "INFO", message: `Test:     curl ${DEFAULT_LB_URL}/` },
  ]);
  const [connHistory, setConnHistory] = useState<number[]>([]);
  const [updateCount, setUpdateCount] = useState(0);

  const addLog = useCallback((level: LogEntry["level"], message: string) => {
    setLogs((prev) => {
      const next = [...prev, { id: logIdCounter++, ts: new Date(), level, message }];
      return next.slice(-100);
    });
  }, []);

  // Every snapshot, streamed or polled, goes through here. Health changes are
  // logged at once; connection counts are logged by the sampler below, since a
  // busy stream sends up to 10 snapshots a second.
  const ingest = useCallback(
    (next: StatsResponse) => {
      const prev = latestRef.current;
      if (!prev) {
        const healthy = next.backends.filter((b) => b.healthy).length;
        addLog("INFO", `Connected: ${next.backends.length} backends, ${healthy} healthy, ${totalConnections(next)} active conns`);
      } else {
        next.backends.forEach((nb, i) => {
          const pb = prev.backends[i];
          if (!pb) return;
          if (pb.healthy && !nb.healthy) addLog("WARN", `Backend :${nb.port} is DOWN, removed from rotation`);
          else if (!pb.healthy && nb.healthy) addLog("INFO", `Backend :${nb.port} is UP, added back to rotation`);
        });
      }
      latestRef.current = next;
      setData(next);
      setUpdateCount((c) => c + 1);
    },
    [addLog]
  );

  useEffect(() => {
    if (!streamUrl || typeof EventSource === "undefined") return;
    const es = new EventSource(streamUrl);
    let open = false;
    es.onmessage = (e) => {
      let next: StatsResponse;
      try {
        next = JSON.parse(e.data);
      } catch {
        return;
      }
      if (!open) {
        open = true;
        setLiveUrl(streamUrl);
        addLog("INFO", `Live stream connected: ${streamUrl}`);
      }
      ingest(next);
    };
    es.onerror = () => {
      if (!open) return; // still connecting; polling covers it meanwhile
      open = false;
      setLiveUrl(null);
      addLog("WARN", "Live stream dropped. Polling until it reconnects.");
    };
    return () => es.close();
  }, [streamUrl, ingest, addLog]);

  // Polling fallback: a null key turns SWR off while the stream is live.
  const { error: pollError } = useSWR<StatsResponse>(live ? null : url, fetcher, {
    refreshInterval: 2000,
    onSuccess: ingest,
    onError: (err) => addLog("ERR", `Cannot reach ${url}: ${err.message}`),
  });
  const error: Error | undefined = live ? undefined : pollError;

  // Samples the latest snapshot on a steady clock for the chart and the
  // connection-count log, however often snapshots arrive.
  useEffect(() => {
    const id = setInterval(() => {
      const cur = latestRef.current;
      if (!cur) return;
      setConnHistory((h) => [...h.slice(-39), totalConnections(cur)]);
      const prev = sampledRef.current;
      if (prev && prev !== cur) {
        cur.backends.forEach((nb, i) => {
          const pb = prev.backends[i];
          if (!pb) return;
          const delta = nb.connections - pb.connections;
          if (delta !== 0)
            addLog("INFO", `Backend :${nb.port} conns ${delta > 0 ? "+" : ""}${delta} → ${nb.connections}`);
        });
      }
      sampledRef.current = cur;
    }, HISTORY_INTERVAL_SEC * 1000);
    return () => clearInterval(id);
  }, [addLog]);

  return { data, error, isLoading: !data && !error, logs, connHistory, updateCount, live, addLog };
}
