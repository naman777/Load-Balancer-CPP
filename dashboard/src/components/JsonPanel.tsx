"use client";

interface Props {
  data: unknown;
  error: unknown;
}

const HELP = `Cannot reach the stats endpoint.

Start the load balancer:
  ./scripts/start.sh

Or manually:
  ./load-balancer/load_balancer --config lb.conf`;

export default function JsonPanel({ data, error }: Props) {
  const text = error ? HELP : data ? JSON.stringify(data, null, 2) : "Waiting for data…";

  return (
    <pre
      tabIndex={0}
      className={`h-72 overflow-auto rounded-xl border border-card-edge bg-card-fill p-4 font-mono text-xs leading-relaxed outline-none focus-visible:ring-2 focus-visible:ring-ring/50 ${
        error ? "text-red-700 dark:text-red-400" : data ? "" : "text-muted-foreground"
      }`}
    >
      {text}
    </pre>
  );
}
