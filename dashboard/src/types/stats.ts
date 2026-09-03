export interface BackendStat {
  port: number;
  connections: number;
  healthy: boolean;
}

export interface StatsResponse {
  port: number;
  backends: BackendStat[];
}

export type Algorithm = "lc" | "rr" | "ih" | "rh";

export interface AlgoMeta {
  key: Algorithm;
  label: string;
  shortLabel: string;
  description: string;
  useCase: string;
  complexity: string;
}

export interface LogEntry {
  id: number;
  ts: Date;
  level: "INFO" | "WARN" | "ERR";
  message: string;
}
