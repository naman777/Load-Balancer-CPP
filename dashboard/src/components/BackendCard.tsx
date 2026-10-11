"use client";

import type { BackendStat } from "@/types/stats";
import { seriesColor } from "@/lib/constants";
import { Card, CardBody } from "@/components/ui/card";
import { Chip } from "@/components/ui/chip";

interface Props {
  backend: BackendStat;
  index: number;
  totalConn: number;
}

export default function BackendCard({ backend, index, totalConn }: Props) {
  const share = totalConn > 0 ? (backend.connections / totalConn) * 100 : 0;

  return (
    <Card className={backend.healthy ? undefined : "sm:opacity-60"}>
      <CardBody className="gap-4">
        <div className="flex items-center justify-between gap-3">
          <span className="flex items-center gap-2 font-mono text-sm font-medium">
            <span
              aria-hidden="true"
              className="size-2.5 rounded-[3px]"
              style={{ background: seriesColor(index) }}
            />
            :{backend.port}
          </span>
          <Chip tone={backend.healthy ? "success" : "danger"}>
            {backend.healthy ? "Healthy" : "Down"}
          </Chip>
        </div>

        <div>
          {/* Re-keyed on change so the new count eases in. */}
          <div
            key={backend.connections}
            className="font-montserrat text-4xl font-semibold tracking-tight motion-safe:animate-chai-fade"
          >
            {backend.connections}
          </div>
          <div className="text-sm text-muted-foreground">active connections</div>
        </div>

        <div>
          <div className="h-1.5 w-full overflow-hidden rounded-full bg-black/[0.08] dark:bg-white/10">
            <div
              className="h-full rounded-full transition-[width] duration-500 ease-out"
              style={{ width: `${share}%`, background: seriesColor(index) }}
            />
          </div>
          <div className="mt-1.5 text-xs text-muted-foreground">
            {share.toFixed(1)}% of traffic
          </div>
        </div>
      </CardBody>
    </Card>
  );
}
