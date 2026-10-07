import { NextRequest, NextResponse } from "next/server";
import { lbUrlForHost } from "@/lib/backend";

export const runtime = "nodejs";

export async function GET(req: NextRequest) {
  const statsUrl =
    process.env.LB_STATS_URL || `${lbUrlForHost(req.headers.get("host"))}/stats`;

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