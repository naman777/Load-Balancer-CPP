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

The proxy core follows nginx's event-driven worker model: a small, fixed number of worker threads, each running its own `epoll` loop over non-blocking sockets. One worker holds thousands of connections at once, so connection count is no longer tied to thread count.

```
                        ┌──────────────────────────────────────────────────┐
                        │               Load Balancer Process              │
                        │                                                  │
  Client                │   SO_REUSEPORT: kernel hashes new connections    │
  connections  ─────────┼──▶ across one listening socket per worker        │
  :8080                 │                                                  │
                        │  ┌───────────────┐ ┌───────────────┐             │
                        │  │   Worker 0    │ │   Worker 1    │  ... × N    │
                        │  │ epoll_wait()  │ │ epoll_wait()  │  (default:  │
                        │  │ ┌───────────┐ │ │ ┌───────────┐ │   1 per     │
                        │  │ │ session   │ │ │ │ session   │ │   core)     │
                        │  │ │ session   │ │ │ │ session   │ │             │
                        │  │ │ ...1000s  │ │ │ │ ...1000s  │ │             │
                        │  │ └───────────┘ │ │ └───────────┘ │             │
                        │  └───────┬───────┘ └───────┬───────┘             │
                        │          └──── select_and_reserve_backend() ─────┤
                        │                (one mutex, held for microseconds)│
                        │  ┌──────────┐  ┌──────────┐                      │
                        │  │ Health   │  │  Stats   │ :8081 ◀── /stats     │
                        │  │ (HEAD/5s)│  │  Thread  │                      │
                        │  └──────────┘  └──────────┘                      │
                        └──────────────────────┬───────────────────────────┘
                                               │
                          ┌────────────────────┼────────────────────┐
                          ▼                    ▼                    ▼
                    Backend :8001        Backend :8002        Backend :8003
                  (echo_server)        (echo_server)        (echo_server)
```

| Thread | Role |
|--------|------|
| Epoll workers (N) | Each owns an epoll instance and a `SO_REUSEPORT` listener; accepts, connects upstream and proxies every session it accepted |
| Health check | Probes each backend via HTTP HEAD every 5 seconds |
| Stats | Serves JSON on `listen_port + 1` |
| Main thread | Calls `start()` and waits for the workers to exit |

**How a worker stays non-blocking (the nginx pattern):**

- **No accept lock.** Every worker binds its own socket to the port with `SO_REUSEPORT`, and the kernel spreads new connections across them. There is no shared accept queue and no thundering herd.
- **Edge-triggered, registered once.** Client and upstream sockets are added with `EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET` exactly once. Per-socket `readable` / `writable` flags remember the last edge until a read or write returns `EAGAIN`, so the hot path makes no `epoll_ctl(MOD)` calls.
- **Backpressure.** Each direction buffers at most 64 KB. When a buffer is full the worker stops reading that side and leaves the rest in the kernel until the other side drains, so a slow client cannot make the LB buffer a large response.
- **Non-blocking upstream connect.** `connect()` returns `EINPROGRESS`, and `EPOLLOUT` + `SO_ERROR` report the result. A refused or timed-out (2 s) connect marks the backend unhealthy and moves on to the next backend.
- **O(1) timers.** Sessions sit in a list ordered by last activity (and connecting sessions in a second list ordered by start time). Once a second the worker only checks the front of each list for the 30 s idle and 2 s connect timeouts.
- **Deferred free.** A session closed while a batch of events is being handled is freed only after the batch, because a later event in the same batch can still point at it.
- **Cached clock.** `now` is read once per `epoll_wait`, as in nginx.

---

## Connection Lifecycle

```
1. Kernel picks a worker's SO_REUSEPORT listener; epoll reports it readable
        │
        ▼
2. accept4(SOCK_NONBLOCK) up to 64 at a time → new Session, client fd added to epoll
        │
        ▼
3. select_and_reserve_backend(client_ip)
   │     └── acquires connection_mutex_, runs algorithm (lc/rr/ih/rh),
   │         increments active_connections[idx], releases lock
   │
   ├── non-blocking connect() to backend → EINPROGRESS, upstream fd added to epoll
   │     └── on refusal, error or 2 s timeout: decrement counter,
   │         mark unhealthy, try the next backend
   │
   ├── client bytes are buffered until the request headers end ("\r\n\r\n"),
   │     then "Connection: close" is injected (non-HTTP data passes through as is)
   │
   ├── event-driven proxying, both directions, with 64 KB backpressure
   │     client ──EPOLLIN──▶ buffer ──EPOLLOUT──▶ backend
   │     backend ──EPOLLIN──▶ buffer ──EPOLLOUT──▶ client
   │     client half-close is passed on with shutdown(SHUT_WR)
   │
   └── backend EOF + response flushed (or error, or 30 s idle):
       close both sockets, decrement active_connections[idx]
```

**Atomicity guarantee:** backend selection and counter increment happen inside a single mutex lock, eliminating the TOCTOU race that would let two workers both pick the same "least loaded" backend.

---

## Features

| Feature | Detail |
|---------|--------|
| **4 algorithms** | Least-connections (weighted), Round-robin (weighted), IP-hash (FNV-1a), Rendezvous/HRW |
| **Epoll event loop** | nginx-style workers: one edge-triggered `epoll` loop per thread, `SO_REUSEPORT` listeners, non-blocking I/O with backpressure; thousands of connections per worker |
| **Weighted backends** | Per-backend weights; heavier servers absorb proportionally more load |
| **Connection cap** | Hard limit per backend; at-capacity backends are skipped automatically |
| **Retry on failure** | If a backend's `connect()` fails, the next healthy backend is tried before dropping the client |
| **HTTP health checks** | Background HTTP HEAD probe every 5 seconds; unhealthy backends are excluded from routing |
| **Stats endpoint** | Live JSON on `:listen_port+1` — active connections and health per backend |
| **SIGHUP reload** | Algorithm and weights reload from config file at runtime without restart |
| **Connection: close injection** | Modifies the first forwarded HTTP request to prevent keep-alive from holding backend slots |
| **Config file** | INI-style `lb.conf`; CLI flags override |
| **Graceful shutdown** | SIGINT/SIGTERM wakes every worker through an `eventfd`; workers close their sessions and exit, then health/stats threads are joined |
| **SO_REUSEADDR** | Immediate restart after crash; no 60-second TIME_WAIT wait |
| **Timestamped logging** | `[YYYY-MM-DD HH:MM:SS] [LEVEL] message` on stdout |

---

## Project Structure

```
.
├── load-balancer/
│   ├── LoadBalancer.h        # Algorithm enum, LoadBalancer class declaration
│   ├── LoadBalancer.cpp      # Epoll workers, proxy state machine, health checks, stats
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
│   └── test_lb.cpp           # 26 tests — algorithms, weights, caps, end-to-end proxying
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
  --workers <n>        Epoll worker threads                 (default: 1 per CPU core)
                       (--threads is still accepted as an alias)
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

# Epoll worker threads (like nginx worker_processes). Omit or 0 = one per CPU core.
# workers  = 4

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
| Event loop (end to end, over loopback) | 400 concurrent round trips with `Connection: close` rewriting checked at the backend; 8 MB response through 64 KB backpressure buffers, byte for byte; failover past a dead backend; **1000 requests parked at a slow backend on 2 workers while a new request is still answered in < 1 s** |

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

## Benchmarks

### Thread pool vs. epoll event loop

Measured on Ubuntu 24.04 under WSL2 (16 logical cores, loopback). The LB, 3 `echo_server` backends and `wrk` (4 threads, 15 s per run, `Connection: close`) all share one machine, so the absolute numbers are capped by the backends and the load generator, not the LB. Both builds ran under the same conditions with least-connections. "Old" is the previous 16-thread pool with `select()` proxying; "new" is the epoll build with 4 workers.

**Fast responses (`GET /`)**

| Concurrent conns | Old: req/s | Old: p99 | New: req/s | New: p99 |
|---|---|---|---|---|
| 100 | 22.0K | 22 ms | 27.4K | 15 ms |
| 500 | 15.6K | 91 ms | 29.5K | 59 ms |
| 1,000 | **crashed** (`FD_SETSIZE` abort) | — | 30.0K | 82 ms |
| 2,000 | **crashed** | — | 31.1K | 121 ms |
| 5,000 | **crashed** | — | 30.4K | 250 ms |
| 10,000 | **crashed** | — | 33.9K | 473 ms |

**Slow backend (`GET /?delay=100`, every response takes 100 ms)**

| Concurrent conns | Old: req/s | Old: p50 | New: req/s | New: p50 |
|---|---|---|---|---|
| 100 | 159 | 603 ms | 987 | 100 ms |
| 500 | 157 | 3.12 s | 4,873 | 100 ms |
| 2,000 | — | — | 17,512 | 106 ms |

The old build tops out at 16 connections ÷ 100 ms ≈ 160 req/s whatever the client count, because each pool thread is blocked for the whole life of one connection. The epoll workers are idle while they wait, so throughput grows with concurrency (c ÷ 100 ms) until the backends saturate.

**Resources:** the epoll build runs 7 threads in total (4 workers + main, health, stats) and stayed at 2–6 MB RSS with 20K open fds at c=10,000. The old build used 19 threads and could not get past ~1,000 fds.

At c=5,000 one thread-per-connection `echo_server` backend segfaulted under its own load. That run's 1,667 read errors come from that backend, not from the LB.

### Other measurements (thread-pool build, earlier runs)

| Metric | Result |
|---|---|
| Added latency, p50 | +~50 µs (58 µs direct vs 108 µs via LB) |
| Added latency, p99 | +~105 µs (~165 µs direct vs ~270 µs via LB) |
| Failover | Backend marked unhealthy ~50 ms after SIGKILL; 0–1 failed requests out of ~67K (the one in flight at the kill) |
| Recovery | Restarted backend receives traffic again after 0.9–3.4 s (5 s health-check cycle) |
| Rendezvous remap (200K keys) | Adding a 4th backend remaps 25.06% of keys (ideal 25%); removing one moves only that backend's keys |
| IP-hash remap | Adding a 4th backend remaps 75% of keys (modulo hashing) |

### Found while load testing

The first epoll build still crashed at ~2,000 connections with the same `FD_SETSIZE` abort. The proxy path no longer used `select()`, but the health checker's connect-with-timeout did, and once thousands of proxied sockets were open, its probe socket's fd number was past 1024. It now uses `poll()`, as does the stats listener.

---

## Prerequisites

- **Compiler:** GCC ≥ 7 or Clang ≥ 5 (C++17 required)
- **OS:** Linux (`epoll`, `eventfd`, `accept4`, `SO_REUSEPORT`); on Windows use WSL2
- **Build tools:** `make` and/or `cmake ≥ 3.14`
- **Testing:** `curl` (for integration tests)

---

## Future Work

- **Dynamic backend add/remove via SIGHUP** — currently algo and weights reload live; changing the backend list requires restart because it would resize vectors while worker threads hold indices into them
- **HTTP/1.1 keep-alive** — requires parsing `Content-Length` and chunked transfer encoding to correctly frame multiple requests per connection

---

## License

MIT License. See [LICENSE](LICENSE) for details.
