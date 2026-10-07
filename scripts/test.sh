#!/usr/bin/env bash
# End-to-end integration test.
# Starts 3 echo backends + the load balancer, exercises them with curl,
# then tears everything down. Exits 0 on success, 1 on any failure.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PASS=0; FAIL=0

# Not ((PASS++)): that returns status 1 when the old value is 0, which trips set -e.
pass() { echo "PASS  $1"; PASS=$((PASS + 1)); }
fail() { echo "FAIL  $1 — $2"; FAIL=$((FAIL + 1)); }

cleanup() {
    pkill -f echo_server   2>/dev/null || true
    pkill -f load_balancer 2>/dev/null || true
    sleep 0.2
}
trap cleanup EXIT

# ── Build ──────────────────────────────────────────────────────────────────────
echo "==> Building..."
(cd "$ROOT/load-balancer" && make -s load_balancer)
(cd "$ROOT/server"        && make -s echo_server)

# ── Start backends ─────────────────────────────────────────────────────────────
cleanup  # kill any stale processes first
for port in 8001 8002 8003; do
    "$ROOT/server/echo_server" "$port" &
done
sleep 0.3

# ── Start load balancer ────────────────────────────────────────────────────────
"$ROOT/load-balancer/load_balancer" \
    --port 8080 --backends 8001,8002,8003 --algo lc --threads 8 &
sleep 0.5

# ── Tests ──────────────────────────────────────────────────────────────────────

# LB responds and returns JSON from a backend
RESP=$(curl -sf http://localhost:8080/ 2>/dev/null || true)
if echo "$RESP" | grep -q '"port"'; then
    pass "LB accepts connections and proxies to backend"
else
    fail "LB accepts connections" "got: $RESP"
fi

# Stats endpoint returns valid JSON
STATS=$(curl -sf http://localhost:8081/stats 2>/dev/null || true)
if echo "$STATS" | grep -q '"backends"'; then
    pass "Stats endpoint returns JSON"
else
    fail "Stats endpoint" "got: $STATS"
fi

# Stats reports 3 backends (count "healthy", since "port" also appears for the LB itself)
BACKEND_COUNT=$(echo "$STATS" | grep -o '"healthy"' | wc -l | tr -d ' ')
if [ "$BACKEND_COUNT" -eq 3 ]; then
    pass "Stats reports all 3 backends"
else
    fail "Stats backend count" "expected 3, got $BACKEND_COUNT"
fi

# Least-connections distributes across multiple backends. The requests must
# overlap (hence the delay): sequential ones always see 0 active connections
# everywhere and all land on the first backend.
PORTS_SEEN=$(for _ in $(seq 9); do curl -sf "http://localhost:8080/?delay=300" 2>/dev/null & done \
             | grep -o '"port":[0-9]*' | sort -u | wc -l | tr -d ' ')
if [ "$PORTS_SEEN" -gt 1 ]; then
    pass "Requests distributed across multiple backends ($PORTS_SEEN backends used)"
else
    fail "Load distribution" "all 9 requests went to the same backend"
fi

# ── Summary ────────────────────────────────────────────────────────────────────
echo ""
echo "Results: $PASS passed, $FAIL failed."
[ "$FAIL" -eq 0 ]
