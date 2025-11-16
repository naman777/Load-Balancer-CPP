# Load Balancer in C++

A production-quality TCP/HTTP load balancer written in C++17. Supports three scheduling algorithms, a fixed-size thread pool, background health checking with HTTP probes, a live JSON stats endpoint, and graceful shutdown.

---

## Features

- **Three algorithms** — least-connections (`lc`), round-robin (`rr`), IP-hash (`ih` via FNV-1a) selectable at startup or via config file
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
  --algo <lc|rr|ih>   Scheduling algorithm (default: lc)
  --threads <n>       Thread pool size (default: 16)
  --help              Show this message
```

CLI flags override values from the config file.

---

## Configuration File (`lb.conf`)

```ini
port     = 8080
backends = 8001, 8002, 8003

# lc = least-connections | rr = round-robin | ih = ip-hash
algo     = lc

threads  = 16
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
| `ih` | IP hash (FNV-1a) | Sticky sessions by client IP |

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

14 tests covering all three selection algorithms (including edge cases: unhealthy backends, single backend, full-unhealthy fallback) and ThreadPool concurrency correctness.

---

## Future Work

- IP-hash with consistent hashing (Rendezvous / Jump hash) for minimal rehash on backend changes
- Weighted backends — allow some servers to receive proportionally more traffic
- Read backend list dynamically from config without restart
- HTTP/1.1 keep-alive support (requires full request/response framing)

---

## License

MIT License. See [LICENSE](LICENSE) for details.
