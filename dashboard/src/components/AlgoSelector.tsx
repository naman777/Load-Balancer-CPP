"use client";

import { ALGO_META } from "@/lib/constants";
import type { Algorithm } from "@/types/stats";
import { RadioGroup } from "@/components/ui/radio-group";

interface Props {
  value: Algorithm;
  onChange: (a: Algorithm) => void;
}

export default function AlgoSelector({ value, onChange }: Props) {
  return (
    <RadioGroup
      aria-label="Balancing algorithm"
      value={value}
      onValueChange={(v) => onChange(v as Algorithm)}
      className="gap-4"
      options={ALGO_META.map((algo) => ({
        value: algo.key,
        label: (
          <span className="flex flex-wrap items-center gap-2 font-medium">
            {algo.label}
            <code className="rounded border border-border px-1.5 font-mono text-[11px] font-normal text-muted-foreground">
              {algo.key}
            </code>
          </span>
        ),
        hint: algo.description,
      }))}
    />
  );
}
