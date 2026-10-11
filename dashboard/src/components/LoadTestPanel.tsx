"use client";

import { useState, useRef } from "react";
import type { BlastResult } from "@/app/api/blast/route";
import { Alert } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import { Field } from "@/components/ui/field";
import { Input } from "@/components/ui/input";
import { Progress } from "@/components/ui/progress";
import { Select } from "@/components/ui/select";
import { Spinner } from "@/components/ui/spinner";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";

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
  const [holdMs, setHoldMs] = useState(0);
  const [url, setUrl] = useState(lbUrl);
  const [firing, setFiring] = useState(false);
  const [results, setResults] = useState<BlastResult[]>([]);
  const [summary, setSummary] = useState<Summary | null>(null);
  const [failure, setFailure] = useState<string | null>(null);
  const listRef = useRef<HTMLDivElement>(null);

  function targetWithDemoDelay() {
    if (!holdMs) return url;
    const target = new URL(url);
    target.searchParams.set("delay", String(holdMs));
    return target.toString();
  }

  async function fire() {
    setFiring(true);
    setResults([]);
    setSummary(null);
    setFailure(null);

    try {
      const res = await fetch("/api/blast", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ count, url: targetWithDemoDelay() }),
      });
      const data = await res.json();
      if (!res.ok || !Array.isArray(data.results)) {
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      // Animate results in one by one for visual effect
      const all: BlastResult[] = data.results;
      setSummary(data.summary);

      for (let i = 0; i < all.length; i++) {
        await new Promise<void>((r) => setTimeout(r, 30));
        setResults((prev) => [...prev, all[i]]);
        if (listRef.current) listRef.current.scrollTop = listRef.current.scrollHeight;
      }

      onFired?.();
    } catch (err) {
      console.error(err);
      setFailure(err instanceof Error ? err.message : String(err));
    } finally {
      setFiring(false);
    }
  }

  const successPct = summary ? Math.round((summary.success / summary.total) * 100) : 0;
  const slowest = Math.max(...results.map((r) => r.ms), 1);

  const tiles = summary
    ? [
        { label: "Succeeded", value: String(summary.success) },
        { label: "Failed", value: String(summary.failed), bad: summary.failed > 0 },
        { label: "Average", value: `${summary.avgMs} ms` },
        { label: "Fastest", value: `${summary.minMs} ms` },
        { label: "Slowest", value: `${summary.maxMs} ms` },
      ]
    : [];

  return (
    <div className="flex flex-col gap-6">
      {/* Controls */}
      <div className="grid gap-5 lg:grid-cols-[minmax(0,1.4fr)_minmax(0,1fr)_auto_auto] lg:items-start">
        <Field label="Target URL" hint="The load balancer frontend">
          <Input
            className="font-mono"
            value={url}
            onChange={(e) => setUrl(e.target.value)}
            placeholder={lbUrl}
            disabled={firing}
          />
        </Field>

        <Field label="Concurrent requests" aside={<span className="font-semibold text-foreground tabular-nums">{count}</span>}>
          <input
            type="range"
            min={1}
            max={100}
            value={count}
            onChange={(e) => setCount(Number(e.target.value))}
            disabled={firing}
            className="h-9 w-full cursor-pointer accent-neutral-900 disabled:cursor-not-allowed disabled:opacity-50 dark:accent-neutral-200"
          />
        </Field>

        <Field label="Response hold">
          <Select value={holdMs} onChange={(e) => setHoldMs(Number(e.target.value))} disabled={firing}>
            <option value={0}>Instant</option>
            <option value={1000}>1 second</option>
            <option value={3000}>3 seconds</option>
            <option value={5000}>5 seconds</option>
          </Select>
        </Field>

        <Button onClick={fire} disabled={firing} type="button" className="min-w-40 lg:mt-[26px]">
          {firing ? (
            <>
              <Spinner label="Firing requests" className="text-current" />
              Firing…
            </>
          ) : (
            <>Fire {count} {count === 1 ? "request" : "requests"}</>
          )}
        </Button>
      </div>

      {failure && (
        <Alert tone="danger" title="The load test did not run">
          {failure}
        </Alert>
      )}

      {/* Summary */}
      {summary && (
        <div className="flex flex-col gap-4 border-t border-border pt-6">
          <dl className="grid grid-cols-2 gap-x-6 gap-y-5 sm:grid-cols-5">
            {tiles.map((t) => (
              <div key={t.label} className="flex flex-col-reverse gap-0.5">
                <dt className="text-xs text-muted-foreground">{t.label}</dt>
                <dd className={`font-montserrat text-2xl font-semibold tracking-tight ${t.bad ? "text-red-700 dark:text-red-400" : ""}`}>
                  {t.value}
                </dd>
              </div>
            ))}
          </dl>
          <div className="flex items-center gap-3">
            <Progress value={successPct} aria-label="Success rate" />
            <span className="shrink-0 text-xs text-muted-foreground">
              <span className="font-semibold text-foreground tabular-nums">{successPct}%</span> success rate
            </span>
          </div>
        </div>
      )}

      {/* Results list */}
      {results.length > 0 && (
        <div ref={listRef} className="max-h-80 overflow-y-auto rounded-xl">
          <Table className="font-mono text-xs">
            <TableHeader>
              <TableRow>
                <TableHead className="w-12">#</TableHead>
                <TableHead className="w-20">Status</TableHead>
                <TableHead className="w-40">Time</TableHead>
                <TableHead className="min-w-40">Response</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {results.map((r) => (
                <TableRow key={r.id}>
                  <TableCell className="text-muted-foreground tabular-nums">{String(r.id).padStart(2, "0")}</TableCell>
                  <TableCell className={r.ok ? "text-green-700 dark:text-green-400" : "text-red-700 dark:text-red-400"}>
                    {r.status > 0 ? r.status : "ERR"}
                  </TableCell>
                  <TableCell>
                    <span className="flex items-center gap-2 whitespace-nowrap tabular-nums">
                      <span
                        aria-hidden="true"
                        className="h-1 rounded-full bg-foreground/40"
                        style={{ width: `${Math.max((r.ms / slowest) * 64, 2)}px` }}
                      />
                      {r.ms} ms
                    </span>
                  </TableCell>
                  <TableCell className="max-w-0 truncate text-muted-foreground">{r.error ?? r.body}</TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </div>
      )}

      {!firing && !summary && !failure && (
        <p className="border-t border-border pt-5 text-sm text-muted-foreground">
          Nothing fired yet. Results land here, and the backend connection counts above move as the requests arrive.
        </p>
      )}
    </div>
  );
}
