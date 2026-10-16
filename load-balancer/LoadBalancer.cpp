#include "LoadBalancer.h"
#include "backend_selector.h"
#include "logger.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <list>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

using Clock = std::chrono::steady_clock;

static constexpr size_t kReadChunk      = 16 * 1024;
static constexpr size_t kMaxBuffered    = 64 * 1024; // per direction; reading pauses above this
static constexpr size_t kMaxHeaderBytes = 16 * 1024; // stop waiting for "\r\n\r\n" past this
static constexpr int    kMaxEvents      = 512;
static constexpr int    kAcceptBatch    = 64;        // accepts per wakeup, so one busy listener can't starve sessions
static constexpr auto   kIdleTimeout    = std::chrono::seconds(30);
static constexpr auto   kConnectTimeout = std::chrono::seconds(2);

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void set_nodelay(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

static sockaddr_in loopback_addr(int port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    return addr;
}

// Reads from a non-blocking socket into `buf` until the socket would block,
// hits EOF, or `buf` holds kMaxBuffered unsent bytes (backpressure: the rest
// stays in the kernel until the other side drains). Clears `readable` only on
// EAGAIN, which is what edge-triggered epoll requires before the next edge.
// Returns false on a hard socket error.
static bool fill(int fd, bool& readable, bool& eof, std::string& buf, size_t sent) {
    char chunk[kReadChunk];
    while (readable && !eof && buf.size() - sent < kMaxBuffered) {
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n > 0)  { buf.append(chunk, static_cast<size_t>(n)); continue; }
        if (n == 0) { eof = true; break; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) { readable = false; break; }
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

// Writes the unsent part of `buf` (from offset `sent`) until done or the
// socket would block. Returns false on a hard socket error.
static bool drain(int fd, bool& writable, std::string& buf, size_t& sent) {
    while (writable && sent < buf.size()) {
        ssize_t n = send(fd, buf.data() + sent, buf.size() - sent, MSG_NOSIGNAL);
        if (n > 0) { sent += static_cast<size_t>(n); continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { writable = false; break; }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    if (sent == buf.size()) { buf.clear(); sent = 0; }
    return true;
}

// Injects "Connection: close" into the first HTTP request buffer so the
// backend closes the connection after sending its response. This prevents
// keep-alive sessions from holding a backend slot across multiple requests.
// Called once the buffered request contains the end of its headers.
static std::string inject_connection_close(const char* data, ssize_t len) {
    std::string req(data, len);
    const size_t hdr_end = req.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return req; // incomplete headers — forward as-is

    std::string headers = req.substr(0, hdr_end);
    const std::string body_and_end = req.substr(hdr_end); // includes \r\n\r\n

    // Remove any existing Connection header (case-insensitive).
    for (size_t pos = 0;;) {
        pos = headers.find("\r\n", pos);
        if (pos == std::string::npos) break;
        size_t line_start = pos + 2;
        size_t line_end = headers.find("\r\n", line_start);
        if (line_end == std::string::npos) line_end = headers.size();
        std::string line = headers.substr(line_start, line_end - line_start);
        // case-insensitive prefix check
        if (line.size() >= 11) {
            std::string prefix = line.substr(0, 11);
            for (auto& c : prefix) c = static_cast<char>(std::tolower(c));
            if (prefix == "connection:") {
                headers.erase(pos, line_end - pos);
                continue;
            }
        }
        pos = line_end;
    }

    return headers + "\r\nConnection: close" + body_and_end;
}

// Connects with a 2-second timeout (non-blocking connect + poll). poll(), not
// select(): with thousands of proxied connections open, this socket's fd number
// is far past FD_SETSIZE, and FD_SET on it aborts the process.
// Returns the connected fd on success, -1 on failure.
static int connect_with_timeout(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int ret = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (ret == 0) return sock; // connected immediately
    if (errno != EINPROGRESS) { close(sock); return -1; }

    pollfd pfd{sock, POLLOUT, 0};
    if (poll(&pfd, 1, 2000) <= 0) { close(sock); return -1; }

    int error = 0;
    socklen_t slen = sizeof(error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &slen);
    if (error != 0) { close(sock); return -1; }
    return sock;
}

// Sends an HTTP HEAD request and checks for a 2xx response.
// Falls back to "healthy" on connection success if no HTTP response is received
// (e.g., raw TCP backend), so this gracefully handles non-HTTP backends too.
static bool http_probe(int port) {
    int sock = connect_with_timeout(port);
    if (sock < 0) return false;

    const char* req = "HEAD / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    send(sock, req, strlen(req), MSG_NOSIGNAL);

    char buf[64] = {};
    ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
    close(sock);

    if (n < 12) return true;  // connected but no HTTP — treat as healthy (raw TCP)
    // "HTTP/1.x 2xx" → healthy
    return (strncmp(buf, "HTTP/1.", 7) == 0 && buf[9] == '2');
}

// ─── Event loop ───────────────────────────────────────────────────────────────
//
// Each Worker is one thread running one epoll loop, the way an nginx worker
// process does. It owns its own SO_REUSEPORT listening socket, so the kernel
// spreads new connections across workers with no accept lock, and a session
// lives on the worker that accepted it for its whole life. The only state
// workers share is the backend table, under connection_mutex_.
//
// Client and upstream sockets are registered once, edge-triggered, for both
// read and write. The readable/writable flags remember what the last edge
// reported until a read or write hits EAGAIN, so the hot path never needs
// epoll_ctl(MOD).

namespace {

struct Session;

// epoll_event.data.ptr points at one of these, so one event says both which
// session it belongs to and which of its sockets fired.
struct Handle {
    enum Kind { LISTEN, WAKE, CLIENT, UPSTREAM } kind;
    Session* session;
};

// One proxied client connection and its upstream connection.
struct Session {
    int      client_fd   = -1;
    int      upstream_fd = -1;
    int      backend     = -1; // reserved index into backend_ports_, -1 = none
    uint32_t client_ip   = 0;
    size_t   attempts    = 0;  // backends tried so far

    bool closed          = false; // waiting in the graveyard to be freed
    bool connecting      = false; // upstream connect() in flight (and in Worker::connecting)
    bool headers_done    = false; // request headers rewritten, or passed through untouched
    bool client_readable = false, client_writable = false, client_eof = false;
    bool up_readable     = false, up_writable     = false, up_eof     = false;
    bool upstream_shut   = false; // client's EOF passed on with shutdown(SHUT_WR)

    std::string to_upstream; size_t to_upstream_sent = 0;
    std::string to_client;   size_t to_client_sent   = 0;

    Handle client_h{Handle::CLIENT, this};
    Handle up_h{Handle::UPSTREAM, this};

    Clock::time_point last_active, connect_started;
    std::list<Session*>::iterator idle_it, connect_it;
};

} // namespace

struct LoadBalancer::Worker {
    LoadBalancer& lb;
    int id;
    int epfd      = -1;
    int listen_fd = -1;
    int wake_fd   = -1;
    Handle listen_h{Handle::LISTEN, nullptr};
    Handle wake_h{Handle::WAKE, nullptr};

    // Timers, kept as lists ordered by deadline so expiry checks only look at
    // the front. Every session is in `idle`, least recently active first;
    // sessions waiting on connect() are also in `connecting`, oldest first.
    std::list<Session*> idle;
    std::list<Session*> connecting;
    // Sessions closed while handling the current batch of events. Freed after
    // the batch, since a later event in the same batch may still point at one.
    std::vector<Session*> graveyard;

    Clock::time_point now; // refreshed once per epoll_wait, like nginx's cached time
    bool accept_paused = false;
    Clock::time_point accept_resume;
    std::thread thread;

    Worker(LoadBalancer& owner, int index) : lb(owner), id(index) {}

    ~Worker() {
        if (thread.joinable()) thread.join();
        if (listen_fd >= 0) close(listen_fd);
        if (wake_fd   >= 0) close(wake_fd);
        if (epfd      >= 0) close(epfd);
    }

    bool init() {
        epfd    = epoll_create1(EPOLL_CLOEXEC);
        wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (epfd < 0 || wake_fd < 0) {
            log_err("Worker " + std::to_string(id) + ": epoll/eventfd failed: " + strerror(errno));
            return false;
        }
        listen_fd = lb.open_listen_socket();
        if (listen_fd < 0) return false;
        // The listener is level-triggered: kAcceptBatch leaves connections
        // queued, and epoll must keep reporting them.
        return watch(listen_fd, EPOLLIN, &listen_h) && watch(wake_fd, EPOLLIN, &wake_h);
    }

    // Wakes the loop so it notices stop_flag_. Async-signal-safe.
    void wake() {
        uint64_t one = 1;
        ssize_t ignored = write(wake_fd, &one, sizeof(one));
        (void)ignored;
    }

    bool watch(int fd, uint32_t events, Handle* h) {
        epoll_event ev{};
        ev.events   = events;
        ev.data.ptr = h;
        return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == 0;
    }

    void run() {
        epoll_event events[kMaxEvents];
        now = Clock::now();
        auto next_sweep = now + std::chrono::seconds(1);

        while (!lb.stop_flag_) {
            int n = epoll_wait(epfd, events, kMaxEvents, 1000);
            if (n < 0 && errno != EINTR) {
                log_err("Worker " + std::to_string(id) + ": epoll_wait failed: " + strerror(errno));
                break;
            }
            now = Clock::now();

            for (int i = 0; i < n; ++i) {
                auto* h = static_cast<Handle*>(events[i].data.ptr);
                switch (h->kind) {
                    case Handle::LISTEN: accept_ready(); break;
                    case Handle::WAKE: {
                        uint64_t v;
                        ssize_t ignored = read(wake_fd, &v, sizeof(v));
                        (void)ignored;
                        break;
                    }
                    case Handle::CLIENT:
                    case Handle::UPSTREAM:
                        if (!h->session->closed)
                            on_socket_event(h->session, h->kind == Handle::CLIENT, events[i].events);
                        break;
                }
            }
            reap();

            if (now >= next_sweep) {
                expire_timers();
                reap();
                next_sweep = now + std::chrono::seconds(1);
            }
        }

        while (!idle.empty()) close_session(idle.front());
        reap();
    }

    // ── Accept ────────────────────────────────────────────────────────────

    void accept_ready() {
        if (accept_paused) return;
        for (int i = 0; i < kAcceptBatch; ++i) {
            sockaddr_in peer{};
            socklen_t len = sizeof(peer);
            int fd = accept4(listen_fd, reinterpret_cast<sockaddr*>(&peer), &len,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                if (errno == EINTR || errno == ECONNABORTED) continue;
                if (errno == EMFILE || errno == ENFILE) {
                    // Out of file descriptors. The level-triggered listener would
                    // spin, so stop watching it for a second (nginx does the same).
                    log_warn("accept(): out of file descriptors — pausing accepts for 1s.");
                    epoll_ctl(epfd, EPOLL_CTL_DEL, listen_fd, nullptr);
                    accept_paused = true;
                    accept_resume = now + std::chrono::seconds(1);
                    return;
                }
                log_err("accept() failed: " + std::string(strerror(errno)));
                return;
            }
            set_nodelay(fd);
            open_session(fd, ntohl(peer.sin_addr.s_addr));
        }
    }

    void open_session(int fd, uint32_t client_ip) {
        auto* s        = new Session;
        s->client_fd   = fd;
        s->client_ip   = client_ip;
        s->last_active = now;
        s->idle_it     = idle.insert(idle.end(), s);

        if (!watch(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, &s->client_h)) {
            log_err("epoll_ctl(client) failed: " + std::string(strerror(errno)));
            close_session(s);
            return;
        }
        connect_upstream(s);
    }

    // ── Upstream connect ──────────────────────────────────────────────────

    // Starts a non-blocking connect to the next backend the algorithm picks.
    // A backend that refuses outright is marked unhealthy and the next one is
    // tried, up to one attempt per backend; the session is closed if all fail.
    void connect_upstream(Session* s) {
        while (s->attempts < lb.backend_ports_.size()) {
            ++s->attempts;
            int idx = lb.select_and_reserve_backend(s->client_ip);
            if (idx < 0) break; // no healthy backend at all

            int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (fd < 0) {
                log_err("socket() failed: " + std::string(strerror(errno)));
                lb.release_backend(idx);
                break; // local resource issue — stop retrying
            }
            set_nodelay(fd);

            sockaddr_in addr = loopback_addr(lb.backend_ports_[idx]);
            if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 &&
                errno != EINPROGRESS) {
                close(fd);
                backend_failed(idx, "unreachable");
                continue;
            }

            s->backend     = idx;
            s->upstream_fd = fd;
            if (!watch(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, &s->up_h)) {
                log_err("epoll_ctl(upstream) failed: " + std::string(strerror(errno)));
                close_session(s);
                return;
            }
            // Even a connect() that finished at once reports EPOLLOUT, so both
            // cases complete in on_socket_event().
            s->connecting      = true;
            s->connect_started = now;
            s->connect_it      = connecting.insert(connecting.end(), s);
            return;
        }
        log_err("All backends unreachable — dropping connection.");
        close_session(s);
    }

    void backend_failed(int idx, const char* why) {
        log_warn("Backend :" + std::to_string(lb.backend_ports_[idx]) + " " + why +
                 " — trying next.");
        lb.release_backend(idx, /*mark_unhealthy=*/true);
    }

    // Drops the in-flight upstream connection and moves on to the next backend.
    void upstream_connect_failed(Session* s, const char* why) {
        connecting.erase(s->connect_it);
        s->connecting = false;
        close(s->upstream_fd); // also removes it from the epoll set
        s->upstream_fd = -1;
        backend_failed(s->backend, why);
        s->backend = -1;
        connect_upstream(s);
    }

    // ── Data path ─────────────────────────────────────────────────────────

    void on_socket_event(Session* s, bool is_client, uint32_t ev) {
        s->last_active = now;
        idle.splice(idle.end(), idle, s->idle_it); // most recently active goes last

        if (is_client) {
            if (ev & EPOLLERR) { close_session(s); return; }
            if (ev & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) s->client_readable = true;
            if (ev & EPOLLOUT) s->client_writable = true;
        } else if (s->connecting) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(s->upstream_fd, SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0 || (ev & EPOLLERR)) {
                upstream_connect_failed(s, "unreachable");
                return;
            }
            if (!(ev & EPOLLOUT)) return; // not finished yet
            connecting.erase(s->connect_it);
            s->connecting  = false;
            s->up_writable = true;
            if (ev & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) s->up_readable = true;
        } else {
            if (ev & EPOLLERR) { close_session(s); return; }
            if (ev & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) s->up_readable = true;
            if (ev & EPOLLOUT) s->up_writable = true;
        }
        pump(s);
    }

    // Moves as much data as both sockets allow, in both directions. Loops
    // because draining a full buffer makes room to read more from a socket
    // that is still readable.
    void pump(Session* s) {
        const bool upstream_ready = !s->connecting && s->upstream_fd >= 0;
        for (;;) {
            // client → upstream
            if (!fill(s->client_fd, s->client_readable, s->client_eof,
                      s->to_upstream, s->to_upstream_sent)) { close_session(s); return; }
            if (!s->headers_done) prepare_request(s);
            bool up_full = s->to_upstream.size() - s->to_upstream_sent >= kMaxBuffered;
            if (s->headers_done && upstream_ready) {
                if (!drain(s->upstream_fd, s->up_writable,
                           s->to_upstream, s->to_upstream_sent)) { close_session(s); return; }
                // Pass a client half-close on once everything it sent is out,
                // so the backend still answers a client that shut down writing.
                if (s->client_eof && s->to_upstream.empty() && !s->upstream_shut) {
                    shutdown(s->upstream_fd, SHUT_WR);
                    s->upstream_shut = true;
                }
            }

            // upstream → client
            bool client_full = false;
            if (upstream_ready) {
                if (!fill(s->upstream_fd, s->up_readable, s->up_eof,
                          s->to_client, s->to_client_sent)) { close_session(s); return; }
                client_full = s->to_client.size() - s->to_client_sent >= kMaxBuffered;
            }
            if (!drain(s->client_fd, s->client_writable,
                       s->to_client, s->to_client_sent)) { close_session(s); return; }

            // The backend closing means the response is complete (we asked for
            // Connection: close), so finish once it has all reached the client.
            if (s->up_eof && s->to_client.empty()) { close_session(s); return; }

            bool again =
                (up_full && s->client_readable &&
                 s->to_upstream.size() - s->to_upstream_sent < kMaxBuffered) ||
                (client_full && s->up_readable &&
                 s->to_client.size() - s->to_client_sent < kMaxBuffered);
            if (!again) return;
        }
    }

    // Holds the start of the client's data until the HTTP request headers are
    // complete, then adds "Connection: close" so the backend ends the session
    // after one response. Anything that is not an HTTP request, or whose
    // headers never end, is passed through as is.
    static void prepare_request(Session* s) {
        std::string& buf = s->to_upstream; // nothing sent yet, so sent offset is 0
        if (buf.empty()) {
            s->headers_done = s->client_eof;
            return;
        }
        if (!std::isupper(static_cast<unsigned char>(buf[0]))) { // not "GET ", "POST ", ...
            s->headers_done = true;
            return;
        }
        if (buf.find("\r\n\r\n") != std::string::npos) {
            buf = inject_connection_close(buf.data(), static_cast<ssize_t>(buf.size()));
            s->headers_done = true;
            return;
        }
        if (buf.size() >= kMaxHeaderBytes || s->client_eof) s->headers_done = true;
    }

    // ── Teardown and timers ───────────────────────────────────────────────

    void close_session(Session* s) {
        if (s->closed) return;
        s->closed = true;
        idle.erase(s->idle_it);
        if (s->connecting) connecting.erase(s->connect_it);
        close(s->client_fd);
        if (s->upstream_fd >= 0) close(s->upstream_fd);
        if (s->backend >= 0) lb.release_backend(s->backend);
        graveyard.push_back(s);
    }

    void reap() {
        for (Session* s : graveyard) delete s;
        graveyard.clear();
    }

    void expire_timers() {
        while (!connecting.empty() && now - connecting.front()->connect_started >= kConnectTimeout)
            upstream_connect_failed(connecting.front(), "connect timed out");
        while (!idle.empty() && now - idle.front()->last_active >= kIdleTimeout)
            close_session(idle.front());
        if (accept_paused && now >= accept_resume) {
            accept_paused = false;
            watch(listen_fd, EPOLLIN, &listen_h);
        }
    }
};

// ─── LoadBalancer ─────────────────────────────────────────────────────────────

static const char* algo_name(Algorithm a) {
    switch (a) {
        case Algorithm::LEAST_CONNECTIONS: return "least-connections";
        case Algorithm::ROUND_ROBIN:       return "round-robin";
        case Algorithm::IP_HASH:           return "ip-hash";
        case Algorithm::RENDEZVOUS:        return "rendezvous";
    }
    return "unknown";
}

LoadBalancer::LoadBalancer(int listen_port, const std::vector<int>& ports,
                           Algorithm algo, size_t worker_count,
                           std::vector<int> weights, int max_conn)
    : listen_port_(listen_port),
      backend_ports_(ports),
      backend_weights_(weights.empty() ? std::vector<int>(ports.size(), 1)
                                       : std::move(weights)),
      active_connections_(ports.size(), 0),
      backend_healthy_(ports.size(), true),
      max_conn_per_backend_(max_conn),
      algorithm_(algo),
      worker_count_(worker_count > 0 ? worker_count
                                     : std::max(1u, std::thread::hardware_concurrency())) {
    rebuild_rr_sequence();
}

void LoadBalancer::rebuild_rr_sequence() {
    rr_sequence_.clear();
    for (size_t i = 0; i < backend_ports_.size(); ++i)
        for (int w = 0; w < backend_weights_[i]; ++w)
            rr_sequence_.push_back(static_cast<int>(i));
    rr_index_ = 0;
}

void LoadBalancer::set_algorithm(Algorithm a) {
    algorithm_.store(a, std::memory_order_relaxed);
    log_info(std::string("Algorithm changed to ") + algo_name(a));
}

void LoadBalancer::set_weights(const std::vector<int>& w) {
    if (w.size() != backend_ports_.size()) {
        log_warn("set_weights: size mismatch (" + std::to_string(w.size()) +
                 " vs " + std::to_string(backend_ports_.size()) + ") — ignored.");
        return;
    }
    std::lock_guard<std::mutex> lock(connection_mutex_);
    backend_weights_ = w;
    rebuild_rr_sequence();
    log_info("Backend weights updated.");
}

LoadBalancer::~LoadBalancer() {
    stop();
}

int LoadBalancer::select_and_reserve_backend(uint32_t client_ip) {
    std::lock_guard<std::mutex> lock(connection_mutex_);

    Algorithm algo = algorithm_.load(std::memory_order_relaxed);
    int mc = max_conn_per_backend_;
    int idx = -1;

    if (algo == Algorithm::ROUND_ROBIN)
        idx = select_weighted_rr(rr_index_, rr_sequence_,
                                 active_connections_, backend_healthy_, mc);
    else if (algo == Algorithm::IP_HASH)
        idx = select_ip_hash(client_ip, backend_healthy_, mc, &active_connections_);
    else if (algo == Algorithm::RENDEZVOUS)
        idx = select_rendezvous(client_ip, backend_healthy_, mc, &active_connections_);
    else
        idx = select_weighted_lc(active_connections_, backend_weights_,
                                 backend_healthy_, mc);

    if (idx >= 0) active_connections_[idx]++;
    return idx;
}

void LoadBalancer::release_backend(int index, bool mark_unhealthy) {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    active_connections_[index]--;
    if (mark_unhealthy) backend_healthy_[index] = false;
}

// Every worker binds its own socket to the same port. SO_REUSEPORT has the
// kernel hash incoming connections across them, like nginx's "listen ... reuseport".
int LoadBalancer::open_listen_socket() {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        log_err("Failed to create server socket: " + std::string(strerror(errno)));
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(listen_port_);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        log_err("Failed to bind on port " + std::to_string(listen_port_) +
                ": " + strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, SOMAXCONN) < 0) {
        log_err("Failed to listen: " + std::string(strerror(errno)));
        close(fd);
        return -1;
    }
    return fd;
}

void LoadBalancer::health_check_loop() {
    while (!stop_flag_) {
        for (size_t i = 0; i < backend_ports_.size(); ++i) {
            if (stop_flag_) return;
            bool healthy = http_probe(backend_ports_[i]);
            std::lock_guard<std::mutex> lock(connection_mutex_);
            if (backend_healthy_[i] != healthy) {
                log_info("Backend :" + std::to_string(backend_ports_[i]) +
                         (healthy ? " is UP" : " is DOWN"));
            }
            backend_healthy_[i] = healthy;
        }
        for (int i = 0; i < 5 && !stop_flag_; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ─── Stats endpoint ───────────────────────────────────────────────────────────
//
// One thread serves listen_port_ + 1 with poll():
//   GET /stats         one JSON snapshot, then close
//   GET /stats/stream  Server-Sent Events: a snapshot on connect, then a new
//                      one whenever the numbers change. Changes are checked
//                      every 100 ms, so a stream gets at most 10 events/s
//                      however busy the proxy is, and a ": ping" comment goes
//                      out after 15 s of silence so proxies keep the stream open.
// It is separate from the epoll workers, so dashboards watching the stats add
// no work to the proxy's hot path.
//
//   curl http://localhost:8081/stats
//   curl -N http://localhost:8081/stats/stream

static constexpr size_t kMaxStatsClients  = 256;       // streams + pending requests
static constexpr size_t kMaxStreamBacklog = 64 * 1024; // drop a stream that stops reading
static constexpr auto   kStatsTick        = std::chrono::milliseconds(100);
static constexpr auto   kStreamKeepalive  = std::chrono::seconds(15);
static constexpr auto   kRequestTimeout   = std::chrono::seconds(2);

// CORS headers — allow browser dashboards on any origin to read the endpoint.
static const char* const kCorsHeaders =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n";

std::string LoadBalancer::stats_json() {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    std::string body = "{\"port\":" + std::to_string(listen_port_) + ",\"backends\":[";
    for (size_t i = 0; i < backend_ports_.size(); ++i) {
        if (i) body += ",";
        body += "{\"port\":"        + std::to_string(backend_ports_[i])  +
                ",\"connections\":" + std::to_string(active_connections_[i]) +
                ",\"healthy\":"     + (backend_healthy_[i] ? "true" : "false") + "}";
    }
    body += "]}";
    return body;
}

namespace {

struct StatsClient {
    int fd = -1;
    Clock::time_point accepted;
    bool streaming = false;
    bool done      = false; // close at the end of this pass
    std::string request;    // bytes read until the end of the headers
    std::string pending;    // stream bytes the socket has not taken yet
};

// Sends what the socket will take now. Returns false if the client is gone.
bool flush_client(StatsClient& c) {
    while (!c.pending.empty()) {
        ssize_t n = send(c.fd, c.pending.data(), c.pending.size(), MSG_NOSIGNAL);
        if (n > 0) { c.pending.erase(0, static_cast<size_t>(n)); continue; }
        if (n < 0 && errno == EINTR) continue;
        return n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
    }
    return true;
}

// For one-shot responses: the socket is fresh and the reply is a few hundred
// bytes, so it fits in the send buffer in one go.
void reply_and_close(StatsClient& c, const std::string& response) {
    ssize_t ignored = send(c.fd, response.data(), response.size(), MSG_NOSIGNAL);
    (void)ignored;
    c.done = true;
}

} // namespace

void LoadBalancer::stats_loop() {
    int stats_port = listen_port_ + 1;
    int listen_fd  = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        log_warn("Could not create stats socket — stats endpoint disabled.");
        return;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(stats_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(listen_fd, 64) < 0) {
        log_warn("Could not bind stats port " + std::to_string(stats_port));
        close(listen_fd);
        return;
    }
    log_info("Stats endpoint listening on port " + std::to_string(stats_port) +
             "  (curl http://localhost:" + std::to_string(stats_port) + "/stats, " +
             "live: /stats/stream)");

    std::vector<StatsClient> clients;
    std::vector<pollfd> pfds;
    std::string last_snapshot = stats_json();
    auto now        = Clock::now();
    auto next_tick  = now + kStatsTick;
    auto last_event = now;

    // Queues an event for every stream; one that has fallen too far behind is dropped.
    auto broadcast = [&](const std::string& event) {
        for (auto& c : clients) {
            if (!c.streaming || c.done) continue;
            if (c.pending.size() > kMaxStreamBacklog) { c.done = true; continue; }
            c.pending += event;
            if (!flush_client(c)) c.done = true;
        }
        last_event = now;
    };

    auto handle_request = [&](StatsClient& c) {
        if (c.request.compare(0, 8, "OPTIONS ") == 0) {
            reply_and_close(c, std::string("HTTP/1.1 204 No Content\r\n") + kCorsHeaders +
                                   "Connection: close\r\n\r\n");
            return;
        }
        if (c.request.compare(0, 18, "GET /stats/stream ") == 0 ||
            c.request.compare(0, 18, "GET /stats/stream?") == 0) {
            c.streaming = true;
            // X-Accel-Buffering stops nginx from holding events back in its buffer.
            c.pending = std::string("HTTP/1.1 200 OK\r\n"
                                    "Content-Type: text/event-stream\r\n"
                                    "Cache-Control: no-cache\r\n"
                                    "X-Accel-Buffering: no\r\n"
                                    "Connection: close\r\n") +
                        kCorsHeaders + "\r\n"
                        "retry: 2000\n\n"
                        "data: " + last_snapshot + "\n\n";
            if (!flush_client(c)) c.done = true;
            return;
        }
        std::string body = stats_json();
        reply_and_close(c, "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: " + std::to_string(body.size()) + "\r\n"
                           "Connection: close\r\n" +
                           std::string(kCorsHeaders) + "\r\n" + body);
    };

    while (!stop_flag_) {
        pfds.clear();
        pfds.push_back({listen_fd, POLLIN, 0});
        for (auto& c : clients) {
            short events = POLLIN; // a stream reads only to notice the client leaving
            if (!c.pending.empty()) events |= POLLOUT;
            pfds.push_back({c.fd, events, 0});
        }

        auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(next_tick - Clock::now());
        int ready = poll(pfds.data(), pfds.size(), std::max<int>(0, static_cast<int>(wait.count())));
        if (ready < 0 && errno != EINTR) {
            log_err("Stats poll() failed: " + std::string(strerror(errno)));
            break;
        }
        now = Clock::now();

        // Existing clients first: pfds[i + 1] belongs to clients[i], and
        // accepting below appends to clients.
        for (size_t i = 0; i < clients.size() && ready > 0; ++i) {
            StatsClient& c = clients[i];
            short re = pfds[i + 1].revents;
            if (!re) continue;
            if (re & (POLLERR | POLLNVAL)) { c.done = true; continue; }

            if (re & (POLLIN | POLLHUP)) {
                char buf[1024];
                ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                    c.done = true;
                    continue;
                }
                if (n > 0 && !c.streaming) {
                    c.request.append(buf, static_cast<size_t>(n));
                    if (c.request.find("\r\n\r\n") != std::string::npos) handle_request(c);
                    else if (c.request.size() > 8192) c.done = true;
                }
            }
            if ((re & POLLOUT) && !flush_client(c)) c.done = true;
        }

        if (pfds[0].revents & POLLIN) {
            for (;;) {
                int fd = accept4(listen_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (fd < 0) break;
                if (clients.size() >= kMaxStatsClients) {
                    std::string busy = std::string("HTTP/1.1 503 Service Unavailable\r\n") +
                                       kCorsHeaders + "Content-Length: 0\r\nConnection: close\r\n\r\n";
                    ssize_t ignored = send(fd, busy.data(), busy.size(), MSG_NOSIGNAL);
                    (void)ignored;
                    close(fd);
                    continue;
                }
                StatsClient c;
                c.fd       = fd;
                c.accepted = now;
                clients.push_back(std::move(c));
            }
        }

        if (now >= next_tick) {
            next_tick = now + kStatsTick;
            std::string snapshot = stats_json();
            if (snapshot != last_snapshot) {
                last_snapshot = snapshot;
                broadcast("data: " + snapshot + "\n\n");
            } else if (now - last_event >= kStreamKeepalive) {
                broadcast(": ping\n\n");
            }
        }

        // A peer that connects and never finishes its request (port scanners
        // do this) is dropped instead of holding a slot.
        for (auto& c : clients)
            if (!c.streaming && !c.done && now - c.accepted >= kRequestTimeout) c.done = true;

        clients.erase(std::remove_if(clients.begin(), clients.end(),
                                     [](const StatsClient& c) {
                                         if (c.done) close(c.fd);
                                         return c.done;
                                     }),
                      clients.end());
    }

    for (auto& c : clients) close(c.fd);
    close(listen_fd);
}

void LoadBalancer::stop() {
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (stop_flag_.exchange(true)) return;
        for (auto& w : workers_) w->wake();
    }
    if (health_check_thread_.joinable()) health_check_thread_.join();
    if (stats_thread_.joinable())        stats_thread_.join();
}

void LoadBalancer::start() {
    {
        std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (stop_flag_) return;

        for (size_t i = 0; i < worker_count_; ++i) {
            auto w = std::make_unique<Worker>(*this, static_cast<int>(i));
            if (!w->init()) {
                workers_.clear();
                return;
            }
            workers_.push_back(std::move(w));
        }

        health_check_thread_ = std::thread(&LoadBalancer::health_check_loop, this);
        stats_thread_        = std::thread(&LoadBalancer::stats_loop, this);
        for (auto& w : workers_) w->thread = std::thread(&Worker::run, w.get());
    }
    log_info("Listening on port " + std::to_string(listen_port_) +
             " with " + std::to_string(backend_ports_.size()) + " backends, " +
             std::to_string(worker_count_) + " epoll workers.");

    // workers_ is not resized again until the destructor, so it is safe to
    // walk without the lock while stop() wakes the workers.
    for (auto& w : workers_) w->thread.join();
}
