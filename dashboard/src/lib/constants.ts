import type { AlgoMeta } from "@/types/stats";

export const ALGO_META: AlgoMeta[] = [
  {
    key: "lc",
    label: "Least Connections",
    shortLabel: "LC",
    description: "Picks the backend with the fewest active connections, weighted by conn/weight ratio.",
    useCase: "Mixed workloads where some requests take much longer than others.",
    complexity: "O(n) scan — atomicity guaranteed via single mutex lock",
  },
  {
    key: "rr",
    label: "Round Robin",
    shortLabel: "RR",
    description: "Cycles through a pre-expanded weight sequence. With weights 2,1,1 the sequence is [A,A,B,C].",
    useCase: "Uniform request durations where you want simple, even distribution.",
    complexity: "O(1) index increment — pre-expanded at startup",
  },
  {
    key: "ih",
    label: "IP Hash",
    shortLabel: "IH",
    description: "Hashes client IP with FNV-1a, maps to backend with modulo, walks forward if unhealthy.",
    useCase: "Sticky sessions on a small, stable cluster. Warning: adding a backend remaps ~all clients.",
    complexity: "O(1) hash — deterministic per client IP",
  },
  {
    key: "rh",
    label: "Rendezvous / HRW",
    shortLabel: "RH",
    description: "Each backend gets score = fnv1a(ip XOR knuth_hash(idx)). Highest score wins. Only ~1/n clients remap on backend add/remove.",
    useCase: "Sticky sessions where the backend list may change with minimal disruption.",
    complexity: "O(n) — beats consistent hash ring in practice due to cache locality",
  },
];

export const BACKEND_COLORS = [
  { from: "#6378dc", to: "#a78bfa", text: "#a78bfa" },
  { from: "#22d3ee", to: "#6378dc", text: "#22d3ee" },
  { from: "#34d399", to: "#22d3ee", text: "#34d399" },
  { from: "#fb923c", to: "#fbbf24", text: "#fb923c" },
];
