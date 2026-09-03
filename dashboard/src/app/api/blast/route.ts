import { NextRequest, NextResponse } from "next/server";

export const runtime = "nodejs";
// Allow up to 30 seconds for a large blast
export const maxDuration = 30;

export interface BlastResult {
  id: number;
  status: number;
  ms: number;
  ok: boolean;
  body: string;
  error?: string;
}

export async function POST(req: NextRequest) {
  const { count, url } = await req.json();

  const n = Math.min(Math.max(Number(count) || 10, 1), 100);
  const target = String(url || "http://13.206.180.74:8080");

  if (!target.startsWith("http://") && !target.startsWith("https://")) {
    return NextResponse.json({ error: "Invalid URL" }, { status: 400 });
  }

  // Fire n concurrent requests
  const promises: Promise<BlastResult>[] = Array.from({ length: n }, (_, i) => {
    const start = Date.now();
    return fetch(target, {
      method: "GET",
      signal: AbortSignal.timeout(8000),
      headers: { "X-Request-Id": String(i + 1) },
    })
      .then(async (r) => {
        const ms = Date.now() - start;
        let body = "";
        try {
          const text = await r.text();
          // Truncate long responses
          body = text.slice(0, 120).replace(/\n/g, " ").trim();
        } catch {
          body = "(no body)";
        }
        return { id: i + 1, status: r.status, ms, ok: r.ok, body } as BlastResult;
      })
      .catch((err: Error) => ({
        id: i + 1,
        status: 0,
        ms: Date.now() - start,
        ok: false,
        body: "",
        error: err.message,
      } as BlastResult));
  });

  const results = await Promise.all(promises);

  const summary = {
    total: results.length,
    success: results.filter((r) => r.ok).length,
    failed: results.filter((r) => !r.ok).length,
    avgMs: Math.round(results.reduce((s, r) => s + r.ms, 0) / results.length),
    minMs: Math.min(...results.map((r) => r.ms)),
    maxMs: Math.max(...results.map((r) => r.ms)),
  };

  return NextResponse.json({ results, summary });
}
