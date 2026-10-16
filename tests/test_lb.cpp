#include "../load-balancer/LoadBalancer.h"
#include "../load-balancer/backend_selector.h"
#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/resource.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

// ── Least-connections ─────────────────────────────────────────────────────────

void test_lc_picks_minimum() {
    std::vector<int>  conns   = {5, 2, 8, 2};
    std::vector<bool> healthy = {true, true, true, true};
    int idx = select_least_connections(conns, healthy);
    assert(idx == 1 || idx == 3); // both have 2 — either is valid
    std::cout << "PASS  lc_picks_minimum\n";
}

void test_lc_skips_unhealthy() {
    std::vector<int>  conns   = {0, 10};
    std::vector<bool> healthy = {false, true};
    assert(select_least_connections(conns, healthy) == 1);
    std::cout << "PASS  lc_skips_unhealthy\n";
}

void test_lc_all_unhealthy() {
    std::vector<int>  conns   = {0, 0, 0};
    std::vector<bool> healthy = {false, false, false};
    assert(select_least_connections(conns, healthy) == -1);
    std::cout << "PASS  lc_all_unhealthy\n";
}

void test_lc_single_backend() {
    std::vector<int>  conns   = {7};
    std::vector<bool> healthy = {true};
    assert(select_least_connections(conns, healthy) == 0);
    std::cout << "PASS  lc_single_backend\n";
}

// ── Weighted least-connections ────────────────────────────────────────────────

void test_weighted_lc_prefers_heavy_backend() {
    // weight=2 backend should absorb twice as many connections before
    // the weight=1 backend is preferred.
    std::vector<int>  conns   = {0, 0};
    std::vector<int>  weights = {2, 1};
    std::vector<bool> healthy = {true, true};
    // ratio: 0/2=0.0, 0/1=0.0 → tie; first wins (index 0)
    assert(select_weighted_lc(conns, weights, healthy) == 0);
    // After 1 connection to 0: ratios 1/2=0.5 vs 0/1=0.0 → pick 1
    conns[0] = 1;
    assert(select_weighted_lc(conns, weights, healthy) == 1);
    // After 1 on each: 1/2=0.5 vs 1/1=1.0 → pick 0
    conns[1] = 1;
    assert(select_weighted_lc(conns, weights, healthy) == 0);
    std::cout << "PASS  weighted_lc_prefers_heavy_backend\n";
}

void test_weighted_lc_skips_unhealthy() {
    std::vector<int>  conns   = {0, 0};
    std::vector<int>  weights = {2, 1};
    std::vector<bool> healthy = {false, true};
    assert(select_weighted_lc(conns, weights, healthy) == 1);
    std::cout << "PASS  weighted_lc_skips_unhealthy\n";
}

void test_weighted_lc_uniform_weights_matches_lc() {
    std::vector<int>  conns   = {3, 1, 5};
    std::vector<int>  weights = {1, 1, 1};
    std::vector<bool> healthy = {true, true, true};
    assert(select_weighted_lc(conns, weights, healthy) ==
           select_least_connections(conns, healthy));
    std::cout << "PASS  weighted_lc_uniform_weights_matches_lc\n";
}

// ── Connection cap (max_conn) ─────────────────────────────────────────────────

void test_lc_respects_max_conn() {
    // Both backends at cap — should return -1.
    std::vector<int>  conns   = {5, 5};
    std::vector<int>  weights = {1, 1};
    std::vector<bool> healthy = {true, true};
    assert(select_weighted_lc(conns, weights, healthy, /*max_conn=*/5) == -1);
    std::cout << "PASS  lc_respects_max_conn\n";
}

void test_lc_routes_to_uncapped_backend() {
    std::vector<int>  conns   = {5, 3};
    std::vector<int>  weights = {1, 1};
    std::vector<bool> healthy = {true, true};
    // Backend 0 is at cap (5), backend 1 is not — must pick backend 1.
    assert(select_weighted_lc(conns, weights, healthy, /*max_conn=*/5) == 1);
    std::cout << "PASS  lc_routes_to_uncapped_backend\n";
}

// ── Round-robin ───────────────────────────────────────────────────────────────

// Plain round-robin is weighted round-robin with equal weights, i.e. the
// sequence [0,1,...,n-1].

void test_rr_cycles_all_backends() {
    std::vector<int>  seq     = {0, 1, 2};
    std::vector<int>  conns   = {0, 0, 0};
    std::vector<bool> healthy = {true, true, true};
    int idx = 0;
    assert(select_weighted_rr(idx, seq, conns, healthy) == 0);
    assert(select_weighted_rr(idx, seq, conns, healthy) == 1);
    assert(select_weighted_rr(idx, seq, conns, healthy) == 2);
    assert(select_weighted_rr(idx, seq, conns, healthy) == 0); // wraps
    std::cout << "PASS  rr_cycles_all_backends\n";
}

void test_rr_skips_unhealthy() {
    std::vector<int>  seq     = {0, 1, 2};
    std::vector<int>  conns   = {0, 0, 0};
    std::vector<bool> healthy = {true, false, true};
    int idx = 0;
    assert(select_weighted_rr(idx, seq, conns, healthy) == 0);
    assert(select_weighted_rr(idx, seq, conns, healthy) == 2); // skips index 1
    std::cout << "PASS  rr_skips_unhealthy\n";
}

void test_rr_all_unhealthy() {
    std::vector<int>  seq     = {0, 1};
    std::vector<int>  conns   = {0, 0};
    std::vector<bool> healthy = {false, false};
    int idx = 0;
    assert(select_weighted_rr(idx, seq, conns, healthy) == -1);
    std::cout << "PASS  rr_all_unhealthy\n";
}

// ── Weighted round-robin ──────────────────────────────────────────────────────

void test_weighted_rr_proportional_distribution() {
    // sequence for weights [2,1]: [0,0,1] — backend 0 gets 2/3 of traffic.
    std::vector<int>  seq     = {0, 0, 1};
    std::vector<int>  conns   = {0, 0};
    std::vector<bool> healthy = {true, true};
    int idx = 0;
    int counts[2] = {0, 0};
    for (int i = 0; i < 6; ++i) {
        int b = select_weighted_rr(idx, seq, conns, healthy);
        assert(b >= 0); counts[b]++;
    }
    assert(counts[0] == 4 && counts[1] == 2);
    std::cout << "PASS  weighted_rr_proportional_distribution\n";
}

void test_weighted_rr_skips_unhealthy() {
    std::vector<int>  seq     = {0, 0, 1};
    std::vector<int>  conns   = {0, 0};
    std::vector<bool> healthy = {false, true};
    int idx = 0;
    for (int i = 0; i < 3; ++i)
        assert(select_weighted_rr(idx, seq, conns, healthy) == 1);
    std::cout << "PASS  weighted_rr_skips_unhealthy\n";
}

void test_weighted_rr_respects_max_conn() {
    std::vector<int>  seq     = {0, 1};
    std::vector<int>  conns   = {5, 2};
    std::vector<bool> healthy = {true, true};
    int idx = 0;
    // Backend 0 is at cap — must skip to backend 1.
    assert(select_weighted_rr(idx, seq, conns, healthy, /*max_conn=*/5) == 1);
    std::cout << "PASS  weighted_rr_respects_max_conn\n";
}

// ── IP-hash ───────────────────────────────────────────────────────────────────

void test_ip_hash_deterministic() {
    std::vector<bool> healthy = {true, true, true};
    uint32_t ip = 0xC0A80101; // 192.168.1.1
    assert(select_ip_hash(ip, healthy) == select_ip_hash(ip, healthy));
    std::cout << "PASS  ip_hash_deterministic\n";
}

void test_ip_hash_skips_unhealthy() {
    // Mark the first candidate unhealthy — whichever backend FNV-1a picks for
    // this IP, the result must be a different (healthy) backend.
    std::vector<bool> healthy = {true, true, true};
    uint32_t ip = 0xC0A80101;
    int preferred = select_ip_hash(ip, healthy);
    assert(preferred >= 0);

    // Now mark that backend unhealthy and confirm a different one is returned.
    healthy[preferred] = false;
    int fallback = select_ip_hash(ip, healthy);
    assert(fallback >= 0 && fallback != preferred);
    std::cout << "PASS  ip_hash_skips_unhealthy\n";
}

void test_ip_hash_all_unhealthy() {
    std::vector<bool> healthy = {false, false, false};
    assert(select_ip_hash(42, healthy) == -1);
    std::cout << "PASS  ip_hash_all_unhealthy\n";
}

// ── Rendezvous hash ───────────────────────────────────────────────────────────

void test_rendezvous_deterministic() {
    std::vector<bool> healthy = {true, true, true};
    uint32_t ip = 0xC0A80101;
    assert(select_rendezvous(ip, healthy) == select_rendezvous(ip, healthy));
    std::cout << "PASS  rendezvous_deterministic\n";
}

void test_rendezvous_skips_unhealthy() {
    std::vector<bool> healthy = {true, true, true};
    uint32_t ip = 0xC0A80101;
    int preferred = select_rendezvous(ip, healthy);
    healthy[preferred] = false;
    int fallback = select_rendezvous(ip, healthy);
    assert(fallback >= 0 && fallback != preferred);
    std::cout << "PASS  rendezvous_skips_unhealthy\n";
}

void test_rendezvous_all_unhealthy() {
    std::vector<bool> healthy = {false, false, false};
    assert(select_rendezvous(0xDEADBEEFu, healthy) == -1);
    std::cout << "PASS  rendezvous_all_unhealthy\n";
}

void test_rendezvous_minimal_remapping() {
    // Adding a backend should remap only ~1/n of IPs, not all of them.
    // With 3 → 4 backends: expect ~75% of IPs to keep the same backend.
    std::vector<bool> h3 = {true, true, true};
    std::vector<bool> h4 = {true, true, true, true};

    int same = 0;
    const int N = 10000;
    for (int i = 0; i < N; ++i) {
        uint32_t ip = static_cast<uint32_t>(i * 2654435761u); // spread IPs
        if (select_rendezvous(ip, h3) == select_rendezvous(ip, h4))
            same++;
    }
    // Expect ~75% stability (3/4); allow ±5% tolerance.
    double stability = static_cast<double>(same) / N;
    assert(stability > 0.70 && stability < 0.80);
    std::cout << "PASS  rendezvous_minimal_remapping ("
              << static_cast<int>(stability * 100) << "% stable)\n";
}

// ── Event loop, end to end over loopback ─────────────────────────────────────

// Minimal HTTP backend for the proxy tests, one thread per connection. The
// response body is `body_size` bytes of a repeating a–z pattern so tests can
// check it arrived intact. While held, it parks every request except
// "/fast", standing in for a slow upstream.
class TestBackend {
public:
    explicit TestBackend(size_t body_size = 16) : body_size_(body_size) {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof(addr);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), len) < 0 ||
            listen(fd_, SOMAXCONN) < 0 ||
            getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
            std::perror("TestBackend");
            std::exit(1);
        }
        port_     = ntohs(addr.sin_port);
        acceptor_ = std::thread([this] { accept_loop(); });
    }

    ~TestBackend() {
        release();
        shutdown(fd_, SHUT_RDWR); // makes the blocked accept() return
        close(fd_);
        acceptor_.join();
        for (auto& t : handlers_) t.join();
    }

    int  port() const     { return port_; }
    int  requests() const { return requests_; }
    bool every_request_had_connection_close() const { return !bad_connection_header_; }

    void hold() { std::lock_guard<std::mutex> lock(m_); hold_ = true; }
    void release() {
        std::lock_guard<std::mutex> lock(m_);
        hold_ = false;
        cv_.notify_all();
    }

private:
    int fd_   = -1;
    int port_ = 0;
    size_t body_size_;
    std::thread acceptor_;
    std::vector<std::thread> handlers_; // only touched by acceptor_ until it is joined
    std::atomic<int>  requests_{0};
    std::atomic<bool> bad_connection_header_{false};
    std::mutex m_;
    std::condition_variable cv_;
    bool hold_ = false;

    void accept_loop() {
        for (;;) {
            int c = accept(fd_, nullptr, nullptr);
            if (c < 0) return;
            handlers_.emplace_back([this, c] { serve(c); });
        }
    }

    void serve(int c) {
        std::string req;
        char buf[4096];
        while (req.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            req.append(buf, static_cast<size_t>(n));
        }
        if (req.empty()) { close(c); return; } // a bare connect, e.g. wait_for_port()

        const bool health_probe = req.compare(0, 5, "HEAD ") == 0;
        if (!health_probe) {
            ++requests_;
            // The LB must replace the client's keep-alive with exactly one close.
            if (req.find("Connection: close") == std::string::npos ||
                req.find("keep-alive") != std::string::npos)
                bad_connection_header_ = true;
            if (req.compare(0, 10, "GET /fast ") != 0) {
                std::unique_lock<std::mutex> lock(m_);
                cv_.wait(lock, [this] { return !hold_; });
            }
        }

        std::string resp = "HTTP/1.1 200 OK\r\nContent-Length: " +
                           std::to_string(body_size_) + "\r\nConnection: close\r\n\r\n";
        if (!health_probe)
            for (size_t i = 0; i < body_size_; ++i) resp += static_cast<char>('a' + i % 26);

        for (size_t sent = 0; sent < resp.size();) {
            ssize_t n = send(c, resp.data() + sent, resp.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
        close(c);
    }
};

static int connect_loopback(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    timeval tv{10, 0}; // a stuck proxy fails the test instead of hanging it
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

static void send_get(int fd, const std::string& path) {
    // keep-alive on purpose: the LB has to rewrite it.
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: test\r\nConnection: keep-alive\r\n\r\n";
    send(fd, req.data(), req.size(), MSG_NOSIGNAL);
}

static std::string read_to_eof(int fd) {
    std::string resp;
    char buf[65536];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) resp.append(buf, static_cast<size_t>(n));
    close(fd);
    return resp;
}

static std::string http_get(int port, const std::string& path) {
    int fd = connect_loopback(port);
    if (fd < 0) return "";
    send_get(fd, path);
    return read_to_eof(fd);
}

static bool response_ok(const std::string& resp, size_t body_size) {
    if (resp.compare(0, 15, "HTTP/1.1 200 OK") != 0) return false;
    size_t start = resp.find("\r\n\r\n");
    if (start == std::string::npos || resp.size() - start - 4 != body_size) return false;
    for (size_t i = 0; i < body_size; ++i)
        if (resp[start + 4 + i] != static_cast<char>('a' + i % 26)) return false;
    return true;
}

// Runs a LoadBalancer on its own thread for the length of a test.
struct RunningLB {
    LoadBalancer lb;
    std::thread thread;

    RunningLB(int port, const std::vector<int>& backends, size_t workers = 2)
        : lb(port, backends, Algorithm::ROUND_ROBIN, workers) {
        thread = std::thread([this] { lb.start(); });
        for (int i = 0; i < 200; ++i) { // wait up to 2 s for the listener
            int fd = connect_loopback(port);
            if (fd >= 0) { close(fd); return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cerr << "LB did not start listening on " << port << "\n";
        std::exit(1);
    }
    ~RunningLB() {
        lb.stop();
        thread.join();
    }
};

void test_proxy_round_trip() {
    TestBackend backend;
    RunningLB lb(18080, {backend.port()});

    std::atomic<int> ok{0};
    std::vector<std::thread> clients;
    for (int t = 0; t < 8; ++t)
        clients.emplace_back([&ok] {
            for (int i = 0; i < 50; ++i)
                if (response_ok(http_get(18080, "/fast"), 16)) ++ok;
        });
    for (auto& c : clients) c.join();

    assert(ok == 400);
    assert(backend.requests() == 400);
    assert(backend.every_request_had_connection_close());
    std::cout << "PASS  proxy_round_trip\n";
}

void test_proxy_large_response() {
    // Far beyond the per-direction buffer, so backpressure has to kick in.
    const size_t size = 8 * 1024 * 1024;
    TestBackend backend(size);
    RunningLB lb(18090, {backend.port()});
    assert(response_ok(http_get(18090, "/fast"), size));
    std::cout << "PASS  proxy_large_response\n";
}

void test_proxy_fails_over_dead_backend() {
    // A port nothing listens on: bind, read it back, close.
    int probe = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(addr);
    bind(probe, reinterpret_cast<sockaddr*>(&addr), len);
    getsockname(probe, reinterpret_cast<sockaddr*>(&addr), &len);
    int dead_port = ntohs(addr.sin_port);
    close(probe);

    TestBackend backend;
    RunningLB lb(18100, {dead_port, backend.port()});
    for (int i = 0; i < 20; ++i) assert(response_ok(http_get(18100, "/fast"), 16));
    std::cout << "PASS  proxy_fails_over_dead_backend\n";
}

// The old thread pool served one connection per thread, so 16 slow requests
// blocked everything behind them. The event loop holds 1000 parked requests on
// two workers and still answers a new one at once.
void test_proxy_many_slow_connections() {
    const int N = 1000;
    TestBackend backend;
    RunningLB lb(18110, {backend.port()});
    backend.hold();

    std::vector<int> fds;
    for (int i = 0; i < N; ++i) {
        int fd = connect_loopback(18110);
        assert(fd >= 0);
        send_get(fd, "/slow");
        fds.push_back(fd);
    }
    for (int i = 0; i < 1000 && backend.requests() < N; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(backend.requests() == N); // all 1000 are in flight at the same time

    auto t0 = std::chrono::steady_clock::now();
    assert(response_ok(http_get(18110, "/fast"), 16));
    assert(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1));

    backend.release();
    for (int fd : fds) assert(response_ok(read_to_eof(fd), 16));
    std::cout << "PASS  proxy_many_slow_connections\n";
}

int main() {
    test_lc_picks_minimum();
    test_lc_skips_unhealthy();
    test_lc_all_unhealthy();
    test_lc_single_backend();

    test_weighted_lc_prefers_heavy_backend();
    test_weighted_lc_skips_unhealthy();
    test_weighted_lc_uniform_weights_matches_lc();

    test_lc_respects_max_conn();
    test_lc_routes_to_uncapped_backend();

    test_weighted_rr_proportional_distribution();
    test_weighted_rr_skips_unhealthy();
    test_weighted_rr_respects_max_conn();

    test_rr_cycles_all_backends();
    test_rr_skips_unhealthy();
    test_rr_all_unhealthy();

    test_ip_hash_deterministic();
    test_ip_hash_skips_unhealthy();
    test_ip_hash_all_unhealthy();

    test_rendezvous_deterministic();
    test_rendezvous_skips_unhealthy();
    test_rendezvous_all_unhealthy();
    test_rendezvous_minimal_remapping();

    // 1000 parked connections need ~4000 fds across client, LB and backend.
    rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
    test_proxy_round_trip();
    test_proxy_large_response();
    test_proxy_fails_over_dead_backend();
    test_proxy_many_slow_connections();

    std::cout << "\nAll 26 tests passed.\n";
    return 0;
}
