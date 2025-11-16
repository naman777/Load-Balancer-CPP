#!/usr/bin/env bash
# Builds (if needed) and starts 3 echo backend servers + the load balancer.
# Usage: ./scripts/start.sh [--algo rr|lc|ih] [--port 8080]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> Building load balancer..."
(cd "$ROOT/load-balancer" && make -s load_balancer)

echo "==> Building echo server..."
(cd "$ROOT/server" && make -s echo_server)

# Start 3 echo backends
BACKEND_PORTS=(8001 8002 8003)
for port in "${BACKEND_PORTS[@]}"; do
    "$ROOT/server/echo_server" "$port" &
    echo "    echo_server started on port $port (PID $!)"
done

# Start load balancer (pass through any extra args)
"$ROOT/load-balancer/load_balancer" --config "$ROOT/lb.conf" "$@" &
LB_PID=$!
echo "    load_balancer started on port 8080 (PID $LB_PID)"
echo ""
echo "Stats:  curl http://localhost:8081/stats"
echo "Test:   curl http://localhost:8080/"
echo ""
echo "Press Ctrl+C to stop all processes."

trap 'echo ""; echo "Stopping..."; kill 0' INT
wait
