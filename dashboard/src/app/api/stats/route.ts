import { NextResponse } from "next/server";

export const runtime = "nodejs";

const statsUrl = process.env.LB_STATS_URL || "http://13.206.180.74:8081/stats";

export async function GET() {
  try {
    const response = await fetch(statsUrl, {
      signal: AbortSignal.timeout(5000),
      cache: "no-store",
    });

    if (!response.ok) {
      return NextResponse.json(
        { error: `Load balancer returned HTTP ${response.status}` },
        { status: 502 }
      );
    }

    return NextResponse.json(await response.json(), {
      headers: { "Cache-Control": "no-store" },
    });
  } catch {
    return NextResponse.json(
      { error: "Load balancer stats are unavailable" },
      { status: 502 }
    );
  }
}