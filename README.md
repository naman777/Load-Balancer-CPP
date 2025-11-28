# C++ Load Balancer

A production-quality TCP/HTTP load balancer written in C++17 from scratch — no external libraries, no frameworks. Built to demonstrate systems programming depth: lock-free algorithm switching, consistent hashing, HTTP-aware proxying, background health checking, and a live stats endpoint.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Connection Lifecycle](#connection-lifecycle)
3. [Features](#features)
4. [Project Structure](#project-structure)
5. [Quick Start](#quick-start)
6. [Building](#building)
7. [CLI Reference](#cli-reference)
8. [Configuration File](#configuration-file)
9. [Algorithms](#algorithms)
10. [Weighted Backends](#weighted-backends)
11. [Connection Cap](#connection-cap)
12. [Health Checks](#health-checks)
13. [Stats Endpoint](#stats-endpoint)
14. [Live Config Reload (SIGHUP)](#live-config-reload-sighup)
15. [Scripts](#scripts)
16. [Testing](#testing)
17. [Design Decisions](#design-decisions)
18. [Future Work](#future-work)

---

## Architecture Overview

```
                        ┌─────────────────────────────────────────────┐
                        │              Load Balancer Process           │
                        │                                             │
  Client                │  ┌──────────┐    ┌────────────────────────┐│
  connections  ─────────┼─▶│  Accept  │───▶│      Thread Pool       ││
  :8080                 │  │  Loop    │    │  (16 workers default)  ││
                        │  └──────────┘    │                        ││
                        │                  │  handle_client()  × N  ││
                        │  ┌──────────┐    │  ┌──────────────────┐  ││
                        │  │ Health   │    │  │select_and_reserve│  ││
                        │  │ Check    │    │  │   _backend()     │  ││
                        │  │ Thread   │    │  └────────┬─────────┘  ││
                        │  │(HTTP HEAD│    │           │ proxy via  ││
                        │  │ /5 secs) │    │      select() loop     ││
                        │  └──────────┘    └────────────────────────┘│
                        │                                             │
                        │  ┌──────────┐                              │
                        │  │  Stats   │  :8081  ◀── curl /stats     │
                        │  │  Thread  │                              │
                        │  └──────────┘                              │
                        └──────────────────────┬──────────────────────┘
                                               │
                          ┌────────────────────┼────────────────────┐
                          ▼                    ▼                    ▼
                    Backend :8001        Backend :8002        Backend :8003
                  (echo_server)        (echo_server)        (echo_server)
```

The load balancer is a **single process with four threads** (besides worker pool threads):

| Thread | Role |
|--------|------|
| Main thread | `accept()` loop — hands sockets to the thread pool |
| Worker pool (N) | Each worker runs `handle_client()` for one connection |
| Health check | Probes each backend via HTTP HEAD every 5 seconds |
| Stats | Serves JSON on `listen_port + 1` |

---

## Connection Lifecycle

```
1. Client connects to :8080
        │
        ▼
2. accept() returns client_socket
        │
        ▼
3. ThreadPool::enqueue(handle_client)
        │
        ▼
4. handle_client() runs on a worker thread
   ├── getpeername() → extract client IP
   ├── select_and_reserve_backend(client_ip)
   │     └── acquires connection_mutex_
   │         runs algorithm (lc/rr/ih/rh)
   │         increments active_connections[idx]
   │         releases lock
   │
   ├── socket() + connect() to backend
   │     └── on failure: decrement counter, mark unhealthy, retry next backend
   │
   ├── inject "Connection: close" into first HTTP request
   │     (forces backend to close after response → clean end-of-data signal)
   │
   ├── Bidirectional proxy loop (select, 30-second idle timeout)
   │     client ──read──▶ send ──▶ backend
   │     backend ──read──▶ send ──▶ client
   │     (loop until either side closes or timeout)
   │
   └── close both sockets
       decrement active_connections[idx]
       log remaining count
```

**Atomicity guarantee:** backend selection and counter increment happen inside a single mutex lock, eliminating the TOCTOU race that would let two threads both pick the same "least loaded" backend.

---

## Features

| Feature | Detail |
|---------|--------|
| **4 algorithms** | Least-connections (weighted), Round-robin (weighted), IP-hash (FNV-1a), Rendezvous/HRW |
| **Thread pool** | Fixed-size worker pool (default 16) — no unbounded thread spawning |
| **Weighted backends** | Per-backend weights; heavier servers absorb proportionally more load |
| **Connection cap** | Hard limit per backend; at-capacity backends are skipped automatically |
| **Retry on failure** | If a backend's `connect()` fails, the next healthy backend is tried before dropping the client |
| **HTTP health checks** | Background HTTP HEAD probe every 5 seconds; unhealthy backends are excluded from routing |
| **Stats endpoint** | Live JSON on `:listen_port+1` — active connections and health per backend |
| **SIGHUP reload** | Algorithm and weights reload from config file at runtime without restart |
| **Connection: close injection** | Modifies the first forwarded HTTP request to prevent keep-alive from holding backend slots |
| **Config file** | INI-style `lb.conf`; CLI flags override |
| **Graceful shutdown** | SIGINT/SIGTERM closes server socket, joins health/stats threads, drains pool |
| **SO_REUSEADDR** | Immediate restart after crash; no 60-second TIME_WAIT wait |
| **Timestamped logging** | `[YYYY-MM-DD HH:MM:SS] [LEVEL] message` on stdout |

---

## Project Structure

```
.
├── load-balancer/
│   ├── LoadBalancer.h        # ThreadPool, Algorithm enum, LoadBalancer class declaration
│   ├── LoadBalancer.cpp      # Full implementation (~470 lines)
│   ├── backend_selector.h    # Pure, lock-free algorithm functions (testable independently)
│   ├── logger.h              # log_info / log_warn / log_err with timestamps
│   ├── config.h              # INI config file parser (header-only)
│   ├── main.cpp              # CLI argument parsing, SIGHUP thread, entry point
│   ├── Makefile              # Build + test targets; tracks all header dependencies
│   └── CMakeLists.txt        # CMake build (load_balancer + echo_server + client + tests)
│
├── server/
│   ├── server.cpp            # Minimal C++ HTTP echo server (replaces Node.js backends)
│   └── Makefile
│
├── tests/
│   └── test_lb.cpp           # 26 unit tests — algorithms, weights, caps, ThreadPool
│
├── scripts/
│   ├── start.sh              # Build + start 3 echo backends + load balancer
│   ├── stop.sh               # pkill echo_server and load_balancer
│   └── test.sh               # End-to-end integration test using curl
│
├── Makefile                  # Root build: load_balancer + echo_server + client
├── lb.conf                   # Default configuration file
└── client.cpp                # Interactive TCP/HTTP test client
```

### Key design: `backend_selector.h`

All algorithm logic lives in **pure free functions** with no side effects — no sockets, no locks, no I/O. `LoadBalancer.cpp` calls them while holding `connection_mutex_`. This separation means the algorithm functions can be tested directly without spinning up any network infrastructure.

```
backend_selector.h
├── select_weighted_lc()    ← picks min(connections/weight)
├── select_weighted_rr()    ← cycles a pre-expanded weight sequence
├── select_ip_hash()        ← FNV-1a(ip) % n, walk forward for healthy
└── select_rendezvous()     ← max(fnv1a(ip XOR knuth_hash(backend_idx)))
```

---

## Quick Start

```bash
# Clone
git clone https://github.com/naman777/Load-Balancer-CPP.git
cd Load-Balancer-CPP

# Build everything (load_balancer + echo_server + client)
make

# Start 3 echo backends + load balancer
chmod +x scripts/start.sh scripts/stop.sh scripts/test.sh
./scripts/start.sh

# In another terminal — test
curl http://localhost:8080/           # proxied to a backend
curl http://localhost:8081/stats      # live JSON stats

# Stop everything
./scripts/stop.sh
```

---

## Building

### Root Makefile (recommended)

Builds everything from the project root:

```bash
make          # load_balancer + echo_server + client
make test     # unit tests
make clean
```

### Per-component Makefiles

```bash
# Load balancer
cd load-balancer
make              # binary → load-balancer/load_balancer
make test         # build + run 26 unit tests
make clean

# Echo server
cd server
make              # binary → server/echo_server
make clean
```

### CMake

```bash
cmake -S load-balancer -B build
cmake --build build
cd build && ctest --output-on-failure
```

### Manual compile

```bash
cd load-balancer
g++ -std=c++17 -pthread -Wall -Wextra -O2 -c LoadBalancer.cpp main.cpp
g++ -std=c++17 -pthread -o load_balancer LoadBalancer.o main.o
```

---

## CLI Reference

```
./load_balancer [options]

  --config <file>      Load settings from config file before applying other flags
  --port <n>           Listen port                          (default: 8080)
  --backends <p,...>   Comma-separated backend ports        (default: 8001,8002,8003)
  --weights <w,...>    Per-backend weights (same order)     (default: all 1)
  --algo <lc|rr|ih|rh> Scheduling algorithm                 (default: lc)
  --threads <n>        Thread pool size                     (default: 16)
  --max-conn <n>       Max connections per backend; 0=none  (default: 0)
  --help               Show this message
```

**Flag priority:** CLI flags always override config file values.

**Examples:**

```bash
# Least-connections with 3 backends
./load_balancer --port 8080 --backends 8001,8002,8003

# Round-robin with one high-capacity backend (weight 3) and two lighter ones
./load_balancer --algo rr --backends 8001,8002,8003 --weights 3,1,1

# Rendezvous hash for sticky sessions; cap each backend at 50 connections
./load_balancer --algo rh --max-conn 50

# Load from config file, override algorithm at runtime
./load_balancer --config lb.conf --algo ih

# Send SIGHUP to reload algo and weights from lb.conf without restarting
kill -HUP $(pgrep load_balancer)
```

---

## Configuration File

`lb.conf` (INI-style, `key = value`; blank lines and `#` comments ignored):

```ini
# Load balancer configuration
# CLI flags override these values when both are provided.

port     = 8080
backends = 8001, 8002, 8003

# Optional: relative weights per backend (same order as backends).
# A backend with weight=2 gets ~2x the connections of a weight=1 backend.
# weights = 2, 1, 1

# Algorithm: lc (least-connections) | rr (round-robin) | ih (ip-hash) | rh (rendezvous)
algo     = lc

# Worker thread pool size
threads  = 16

# Max concurrent connections routed to each backend; 0 = unlimited.
# max_conn = 100
```

---

## Algorithms

### Least-Connections (`lc`) — default

Picks the backend with the fewest active connections, weighted by `connections / weight`.

```
backends:    A(conns=4, w=2)   B(conns=2, w=1)   C(conns=1, w=1)
ratios:      4/2 = 2.0         2/1 = 2.0         1/1 = 1.0
winner:      C  ← lowest ratio
```

**Best for:** mixed workloads where some requests take much longer than others.

---

### Round-Robin (`rr`)

Cycles through a pre-expanded weight sequence. With `weights = 2,1,1` the sequence is `[A, A, B, C]` and repeats. Unhealthy or capped backends are skipped in the cycle.

```
sequence: A → A → B → C → A → A → B → C → ...
```

**Best for:** uniform request durations where you want simple, even distribution.

---

### IP Hash (`ih`)

Hashes the client IP with FNV-1a, maps to a backend with modulo, walks forward if that backend is unhealthy or capped.

```
backend_index = fnv1a(client_ip) % n
```

**Limitation:** adding or removing one backend remaps almost all clients (birthday problem with modulo).

**Best for:** sticky sessions on a small, stable cluster where the backend list rarely changes.

---

### Rendezvous / HRW (`rh`)

Each backend gets a score `= fnv1a(client_ip XOR knuth_hash(backend_idx))`. The backend with the **highest score** wins. When a backend is added or removed, only `~1/n` of clients get remapped — everyone else sticks to the same backend.

```
scores:   hash(ip, A) = 0x8A3F...   ← winner
          hash(ip, B) = 0x3C12...
          hash(ip, C) = 0x6E90...
```

After adding backend D: only clients whose highest score shifts to D are remapped (~25% with 4 backends).

**Best for:** sticky sessions where the backend list may change and you want minimal session disruption.

---

## Weighted Backends

All four algorithms respect per-backend weights. Weights are relative — only ratios matter.

```bash
# Backend 8001 gets twice as many connections as 8002 and 8003
./load_balancer --backends 8001,8002,8003 --weights 2,1,1
```

In `lb.conf`:
```ini
backends = 8001, 8002, 8003
weights  = 2, 1, 1
```

**How it works per algorithm:**
- `lc`: compares `connections / weight` — higher weight = lower ratio = preferred
- `rr`: pre-expands to sequence `[0,0,1,2]` — backend 0 appears twice per cycle
- `ih` / `rh`: weights are not applied (these algorithms route by IP identity, not load)

Weights can be updated live via SIGHUP without restarting.

---

## Connection Cap

Prevents any single backend from being overloaded by hard-capping active connections:

```bash
./load_balancer --max-conn 100
```

```ini
max_conn = 100
```

When a backend reaches the cap, it is skipped by the algorithm exactly like an unhealthy backend. If all backends are at capacity, the client connection is dropped with a log message.

---

## Health Checks

A background thread probes each backend every **5 seconds** using an HTTP HEAD request:

```
HEAD / HTTP/1.0\r\nHost: localhost\r\n\r\n
```

- A `2xx` response → **healthy** (backend stays in rotation)
- Connection refused / timeout / non-2xx → **unhealthy** (excluded from routing)
- Raw TCP backend (no HTTP response) → treated as **healthy** (graceful fallback)

When a backend transitions between states, it is logged:

```
[2025-04-25 14:32:11] [INFO ] Backend :8002 is DOWN
[2025-04-25 14:32:16] [INFO ] Backend :8002 is UP
```

Existing connections to a backend that goes down are not killed — they finish naturally. New connections simply stop being routed to it.

---

## Stats Endpoint

While the load balancer is running, a separate HTTP endpoint on `listen_port + 1` (default `:8081`) serves a JSON snapshot:

```bash
curl http://localhost:8081/stats
```

```json
{
  "port": 8080,
  "backends": [
    {"port": 8001, "connections": 12, "healthy": true},
    {"port": 8002, "connections":  8, "healthy": true},
    {"port": 8003, "connections":  0, "healthy": false}
  ]
}
```

The data is captured under `connection_mutex_` so it is always consistent. The stats thread handles one request at a time (it's monitoring, not a hot path).

---

## Live Config Reload (SIGHUP)

When started with `--config <file>`, the load balancer can reload its algorithm and backend weights at runtime without dropping any connections:

```bash
# Edit lb.conf — change algo from lc to rr, or adjust weights
./load_balancer --config lb.conf &

# ... later, without restarting:
kill -HUP $(pgrep load_balancer)
```

```
[2025-04-25 14:45:03] [INFO ] Algorithm changed to round-robin
[2025-04-25 14:45:03] [INFO ] Backend weights updated.
[2025-04-25 14:45:03] [INFO ] SIGHUP: config reloaded from lb.conf
```

**What reloads:** algorithm, weights (if count matches backend count).  
**What requires restart:** port, backend list, thread count, connection cap.  
**Implementation:** SIGHUP is blocked in all threads via `pthread_sigmask`; a dedicated thread waits on `sigwait()` — the only signal-safe way to call arbitrary C++ from a signal context. The algorithm field is `std::atomic<Algorithm>` so reads in worker threads never need a lock.

---

## Scripts

### `scripts/start.sh`

Builds both binaries (if needed), starts 3 echo backends and the load balancer as background processes, and waits. Ctrl+C kills everything cleanly.

```bash
./scripts/start.sh                    # default settings from lb.conf
./scripts/start.sh --algo rh          # pass extra flags to load_balancer
```

### `scripts/stop.sh`

```bash
./scripts/stop.sh    # pkill echo_server; pkill load_balancer
```

### `scripts/test.sh`

End-to-end integration test. Builds, starts backends + LB, runs curl checks, asserts results, then tears everything down. Exit code 0 = all pass.

```bash
bash scripts/test.sh
```

Checks performed:
1. LB accepts connections and proxies to a backend
2. Stats endpoint returns valid JSON
3. Stats reports all 3 backends
4. 9 sequential requests reach more than 1 backend (distribution verified)

---

## Testing

### Unit Tests (26 tests)

```bash
cd load-balancer && make test
```

Tests are in `tests/test_lb.cpp` and cover:

| Group | Tests |
|-------|-------|
| Least-connections | picks minimum, skips unhealthy, all unhealthy, single backend |
| Weighted LC | proportional preference, skips unhealthy, matches unweighted when weights=1 |
| Connection cap | blocks at-capacity backend, routes to uncapped alternative |
| Round-robin (weighted) | proportional distribution, skips unhealthy, respects cap |
| IP hash | deterministic, skips unhealthy, all unhealthy |
| Rendezvous | deterministic, skips unhealthy, all unhealthy, **statistical stability** (3→4 backends remaps ~25%, not ~100%) |
| ThreadPool | executes all tasks, concurrent increments, indexed slots, empty pool |

The Rendezvous stability test is particularly interesting — it generates 10,000 random IPs and asserts that ~75% ± 5% keep their backend assignment when a 4th backend is added, proving the consistent hashing property statistically.

### Integration Tests

```bash
bash scripts/test.sh
```

### CI (GitHub Actions)

Every push and pull request runs:
1. `make` — build load balancer
2. `make test` — 26 unit tests
3. `make` in `server/` — build echo server
4. `bash scripts/test.sh` — integration tests
5. `cmake + ctest` — alternate build system verification

---

## Design Decisions

### Why `select_and_reserve_backend` holds the lock across both selection and increment

The original code released the lock between finding the least-loaded backend and incrementing its counter (a classic TOCTOU race). Under concurrency, two threads would both see backend B as "least loaded" and both increment it — defeating the algorithm entirely. The fix is to hold a single lock for the entire read-modify operation.

### Why `std::atomic<Algorithm>` instead of a mutex-protected field

The algorithm field is read on every connection (inside the lock already held for counter access) and written only on SIGHUP. Making it atomic means the SIGHUP thread can update it without acquiring `connection_mutex_` and potentially blocking the accept loop. Reads are `memory_order_relaxed` — the value is always valid, and a slight delay in visibility across cores is harmless for an algorithm selection.

### Why `Connection: close` injection instead of full HTTP framing

Implementing a correct HTTP/1.1 proxy requires parsing `Content-Length` and chunked transfer encoding for every request and response — roughly 200+ lines of state-machine code with many edge cases. Injecting `Connection: close` achieves correctness for the common case (single request per connection) with ~30 lines: the backend closes after its response, which is an unambiguous end-of-data signal. The tradeoff is that HTTP/1.1 pipelining is not supported.

### Why sigwait() for SIGHUP instead of a signal handler

Calling `std::mutex::lock()` or `std::cout` from a POSIX signal handler is undefined behaviour — only async-signal-safe functions are permitted. `sigwait()` solves this by blocking SIGHUP delivery in all threads and letting a dedicated thread consume it synchronously, where arbitrary C++ is safe to call.

### Why Rendezvous hash instead of consistent hash ring

A consistent hash ring (like in Cassandra/Memcached) requires maintaining a sorted ring structure and doing binary search — O(log n) per lookup. Rendezvous hash (HRW) gives the same consistency property with O(n) per lookup (one hash per backend), no auxiliary data structures, and about 5 lines of code. For a load balancer with typically 3–20 backends, O(n) is faster in practice due to cache locality.

---

## Prerequisites

- **Compiler:** GCC ≥ 7 or Clang ≥ 5 (C++17 required)
- **OS:** Linux or macOS (POSIX sockets, `pthread_sigmask`, `sigwait`)
- **Build tools:** `make` and/or `cmake ≥ 3.14`
- **Testing:** `curl` (for integration tests)

---

## Future Work

- **Dynamic backend add/remove via SIGHUP** — currently algo and weights reload live; changing the backend list requires restart because it would resize vectors while worker threads hold indices into them
- **HTTP/1.1 keep-alive** — requires parsing `Content-Length` and chunked transfer encoding to correctly frame multiple requests per connection

---

## License

MIT License. See [LICENSE](LICENSE) for details.
