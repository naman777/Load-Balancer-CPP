import type { AlgoMeta } from "@/types/stats";

export const ALGO_META: AlgoMeta[] = [
  {
    key: "lc",
    label: "Least connections",
    shortLabel: "LC",
    description: "Picks the backend with the fewest active connections, weighted by conn/weight ratio.",
    useCase: "Mixed workloads where some requests take much longer than others.",
    complexity: "O(n) scan, atomic under a single mutex lock",
  },
  {
    key: "rr",
    label: "Round robin",
    shortLabel: "RR",
    description: "Cycles through a pre-expanded weight sequence. With weights 2,1,1 the sequence is [A,A,B,C].",
    useCase: "Uniform request durations where you want simple, even distribution.",
    complexity: "O(1) index increment, pre-expanded at startup",
  },
  {
    key: "ih",
    label: "IP hash",
    shortLabel: "IH",
    description: "Hashes the client IP with FNV-1a, maps it to a backend with modulo, and walks forward if that one is unhealthy.",
    useCase: "Sticky sessions on a small, stable cluster. Adding a backend remaps almost every client.",
    complexity: "O(1) hash, deterministic per client IP",
  },
  {
    key: "rh",
    label: "Rendezvous hashing (HRW)",
    shortLabel: "RH",
    description: "Each backend gets score = fnv1a(ip XOR knuth_hash(idx)) and the highest score wins. Only about 1/n of clients remap when a backend is added or removed.",
    useCase: "Sticky sessions where the backend list may change with minimal disruption.",
    complexity: "O(n), and faster than a consistent hash ring in practice thanks to cache locality",
  },
];

// Backends take a series colour by position, in a fixed order that is never
// cycled: a fifth backend onwards falls back to neutral grey.
const SERIES = ["var(--series-1)", "var(--series-2)", "var(--series-3)", "var(--series-4)"];

export function seriesColor(index: number): string {
  return SERIES[index] ?? "var(--series-other)";
}
