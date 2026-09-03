#!/usr/bin/env bash
# start-all.sh — Wrapper called by systemd. Starts echo backends then the LB.
# The LB process replaces this shell (exec), so systemd tracks it directly.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Kill any stale echo_servers from a previous run
pkill echo_server 2>/dev/null || true
sleep 0.3

# Start 3 echo backends in background
"$ROOT/server/echo_server" 8001 &
"$ROOT/server/echo_server" 8002 &
"$ROOT/server/echo_server" 8003 &

# Give backends a moment to bind their ports
sleep 0.5

# Replace this shell with the load balancer process
# (exec makes systemd track the LB's PID directly)
exec "$ROOT/load-balancer/load_balancer" --config "$ROOT/lb.conf"
