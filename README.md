# Load Balancer in C++

A production-quality TCP/HTTP load balancer written in C++17. Supports three scheduling algorithms, a fixed-size thread pool, background health checking with HTTP probes, a live JSON stats endpoint, and graceful shutdown.

---

## Features

- **Three algorithms** — least-connections (`lc`), round-robin (`rr`), IP-hash (`ih` via FNV-1a) selectable at startup or via config file; all support per-backend **weights** and a **connection cap**
- **Thread pool** — fixed worker pool (default 16), no per-connection thread spawning
- **Bidirectional proxy** via `select()` with a 30-second idle timeout and `Connection: close` injection to prevent keep-alive slot leaks
- **Health checks** — background thread sends HTTP HEAD every 5 seconds; unhealthy backends are skipped without dropping existing connections
- **Stats endpoint** — HTTP JSON on `listen_port + 1` (default `:8081`); shows active connections and health per backend
- **Config file** — `lb.conf` with INI-style `key = value` syntax; CLI flags override file values
- **Graceful shutdown** — SIGINT/SIGTERM drains the accept loop, joins all workers and background threads
- **SO_REUSEADDR** — immediate restart after crash without waiting for TCP TIME_WAIT

---

## Project Structure

```
.
├── load-balancer/
│   ├── LoadBalancer.h        # Class declarations, ThreadPool, Algorithm enum
│   ├── LoadBalancer.cpp      # Full implementation
│   ├── backend_selector.h    # Pure, testable algorithm selection functions
│   ├── logger.h              # Timestamped log_info / log_warn / log_err
│   ├── config.h              # INI config file parser
│   ├── main.cpp              # Entry point with CLI argument parsing
│   ├── Makefile              # Build + test targets
│   └── CMakeLists.txt        # CMake build (also builds echo server + tests)
├── server/
│   ├── server.cpp            # Self-contained C++ HTTP echo server
│   └── Makefile
├── tests/
│   └── test_lb.cpp           # 14 unit tests (algorithms + ThreadPool)
├── scripts/
│   ├── start.sh              # Build + start 3 echo backends + load balancer
│   └── stop.sh               # Kill running instances
├── lb.conf                   # Default configuration file
└── client.cpp                # Simple TCP client for manual testing
```

---

## Quick Start

```bash
# Build everything
cd load-balancer && make && cd ../server && make && cd ..

# Start 3 backends + load balancer in one shot
chmod +x scripts/start.sh scripts/stop.sh
./scripts/start.sh

# Test
curl http://localhost:8080/

# Live stats (active connections + health per backend)
curl http://localhost:8081/stats

# Stop everything
./scripts/stop.sh
```

---

## Building Manually

### Makefile

```bash
cd load-balancer
make              # build load_balancer binary
make test         # build and run unit tests
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
g++ -std=c++17 -pthread -Wall -Wextra -O2 -c LoadBalancer.cpp main.cpp
g++ -std=c++17 -pthread -o load_balancer main.o LoadBalancer.o
```

---

## CLI Usage

```
./load_balancer [options]

  --config <file>     Load settings from config file (default: lb.conf)
  --port <n>          Listen port (default: 8080)
  --backends <p,...>  Comma-separated backend ports (default: 8001,8002,8003)
  --algo <lc|rr|ih|rh> Scheduling algorithm (default: lc)
  --threads <n>       Thread pool size (default: 16)
  --max-conn <n>      Max concurrent connections per backend; 0=unlimited (default: 0)
  --help              Show this message
```

CLI flags override values from the config file.

---

## Configuration File (`lb.conf`)

```ini
port     = 8080
backends = 8001, 8002, 8003

# Optional: relative weights per backend (same order as backends).
# weights = 2, 1, 1

# lc = least-connections | rr = round-robin | ih = ip-hash
algo     = lc

threads  = 16

# Max concurrent connections routed to each backend; 0 = unlimited.
# max_conn = 100
```

---

## Stats Endpoint

While the load balancer is running, hit `http://localhost:8081/stats` for a JSON snapshot:

```json
{
  "port": 8080,
  "backends": [
    {"port": 8001, "connections": 3, "healthy": true},
    {"port": 8002, "connections": 1, "healthy": true},
    {"port": 8003, "connections": 0, "healthy": false}
  ]
}
```

---

## Algorithms

| Flag | Algorithm | Best for |
|------|-----------|----------|
| `lc` | Least connections | Mixed request durations |
| `rr` | Round robin | Uniform request durations |
| `ih` | IP hash (FNV-1a % n) | Sticky sessions, small stable clusters |
| `rh` | Rendezvous / HRW | Sticky sessions with minimal remapping when backends are added or removed |

`rh` (Rendezvous) is strictly better than `ih` for sticky-session use cases: adding or removing one backend remaps only ~1/n of clients, versus nearly all clients with plain modulo hashing.

---

## Prerequisites

- GCC ≥ 7 or Clang ≥ 5 (C++17)
- POSIX-compliant OS (Linux, macOS)
- `make` or `cmake`

---

## Running Unit Tests

```bash
cd load-balancer && make test
```

26 tests covering all four selection algorithms (including Rendezvous remapping stability), weighted variants, per-backend connection cap edge cases, and ThreadPool concurrency correctness.

---

## Future Work

- Dynamic backend add/remove via SIGHUP without restart (currently only algo and weights reload live)
- HTTP/1.1 keep-alive support (requires full Content-Length / chunked transfer encoding parsing)

---

## License

MIT License. See [LICENSE](LICENSE) for details.
