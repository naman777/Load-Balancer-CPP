#include "LoadBalancer.h"
#include "backend_selector.h"
#include "logger.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

// ─── ThreadPool ───────────────────────────────────────────────────────────────

ThreadPool::ThreadPool(size_t threads) {
    for (size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) w.join();
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

static bool send_all(int fd, const char* buf, ssize_t len) {
    ssize_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, static_cast<size_t>(len - sent), MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

// Injects "Connection: close" into the first HTTP request buffer so the
// backend closes the connection after sending its response. This prevents
// keep-alive sessions from holding a backend slot across multiple requests.
// Assumes headers fit within the first read() buffer (true for >99% of requests).
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

// Connects with a 2-second timeout (non-blocking connect + select).
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

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    struct timeval tv{2, 0};
    if (select(sock + 1, nullptr, &wfds, nullptr, &tv) <= 0) { close(sock); return -1; }

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

// ─── LoadBalancer ─────────────────────────────────────────────────────────────

LoadBalancer::LoadBalancer(int listen_port, const std::vector<int>& ports,
                           Algorithm algo, size_t thread_pool_size)
    : listen_port_(listen_port),
      backend_ports_(ports),
      active_connections_(ports.size(), 0),
      backend_healthy_(ports.size(), true),
      algorithm_(algo),
      thread_pool_(thread_pool_size) {}

LoadBalancer::~LoadBalancer() {
    stop();
}

int LoadBalancer::select_and_reserve_backend(uint32_t client_ip) {
    std::lock_guard<std::mutex> lock(connection_mutex_);

    int idx = -1;
    if (algorithm_ == Algorithm::ROUND_ROBIN)
        idx = select_round_robin(rr_index_, backend_ports_.size(), backend_healthy_);
    else if (algorithm_ == Algorithm::IP_HASH)
        idx = select_ip_hash(client_ip, backend_healthy_);
    else
        idx = select_least_connections(active_connections_, backend_healthy_);

    if (idx >= 0) active_connections_[idx]++;
    return idx;
}

void LoadBalancer::handle_client(int client_socket) {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    getpeername(client_socket, reinterpret_cast<sockaddr*>(&peer), &peer_len);

    int index = select_and_reserve_backend(ntohl(peer.sin_addr.s_addr));
    if (index < 0) {
        log_warn("No healthy backend — dropping connection.");
        close(client_socket);
        return;
    }

    int backend_port = backend_ports_[index];
    int backend_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_socket < 0) {
        log_err("Failed to create backend socket: " + std::string(strerror(errno)));
        std::lock_guard<std::mutex> lock(connection_mutex_);
        active_connections_[index]--;
        close(client_socket);
        return;
    }

    sockaddr_in backend_addr{};
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(backend_port);
    inet_pton(AF_INET, "127.0.0.1", &backend_addr.sin_addr);

    if (connect(backend_socket, reinterpret_cast<sockaddr*>(&backend_addr),
                sizeof(backend_addr)) < 0) {
        log_err("Failed to connect to backend port " + std::to_string(backend_port) +
                ": " + strerror(errno));
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            active_connections_[index]--;
            backend_healthy_[index] = false;
        }
        close(client_socket);
        close(backend_socket);
        return;
    }

    // Bidirectional forwarding via select() — 30-second idle timeout.
    // On the first client→backend read we inject "Connection: close" so the
    // backend terminates the connection after its response, giving us a clean
    // end-of-response signal without parsing Content-Length or chunked encoding.
    char buffer[4096];
    int maxfd = std::max(client_socket, backend_socket) + 1;
    bool first_client_read = true;

    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        FD_SET(backend_socket, &read_fds);
        struct timeval timeout{30, 0};

        int ready = select(maxfd, &read_fds, nullptr, nullptr, &timeout);
        if (ready <= 0) break;

        if (FD_ISSET(client_socket, &read_fds)) {
            ssize_t n = read(client_socket, buffer, sizeof(buffer));
            if (n <= 0) break;
            if (first_client_read) {
                std::string modified = inject_connection_close(buffer, n);
                if (!send_all(backend_socket, modified.data(),
                              static_cast<ssize_t>(modified.size()))) break;
                first_client_read = false;
            } else {
                if (!send_all(backend_socket, buffer, n)) break;
            }
        }
        if (FD_ISSET(backend_socket, &read_fds)) {
            ssize_t n = read(backend_socket, buffer, sizeof(buffer));
            if (n <= 0) break;
            if (!send_all(client_socket, buffer, n)) break;
        }
    }

    close(client_socket);
    close(backend_socket);

    int remaining;
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        active_connections_[index]--;
        remaining = active_connections_[index];
    }
    log_info("Backend :" + std::to_string(backend_port) +
             " connection closed. Active: " + std::to_string(remaining));
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

// Serves a simple JSON stats page on listen_port_ + 1.
// curl http://localhost:8081/stats
void LoadBalancer::stats_loop() {
    int stats_port = listen_port_ + 1;
    stats_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (stats_socket_ < 0) {
        log_warn("Could not create stats socket — stats endpoint disabled.");
        return;
    }
    int opt = 1;
    setsockopt(stats_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(stats_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(stats_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        listen(stats_socket_, 16) < 0) {
        log_warn("Could not bind stats port " + std::to_string(stats_port));
        close(stats_socket_);
        stats_socket_ = -1;
        return;
    }
    log_info("Stats endpoint listening on port " + std::to_string(stats_port) +
             "  (curl http://localhost:" + std::to_string(stats_port) + "/stats)");

    while (!stop_flag_) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(stats_socket_, &rfds);
        struct timeval tv{1, 0};
        if (select(stats_socket_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) continue;

        int client = accept(stats_socket_, nullptr, nullptr);
        if (client < 0) continue;

        char req[256];
        read(client, req, sizeof(req) - 1); // drain the request

        std::string body;
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            body = "{\"port\":" + std::to_string(listen_port_) + ",\"backends\":[";
            for (size_t i = 0; i < backend_ports_.size(); ++i) {
                if (i) body += ",";
                body += "{\"port\":"        + std::to_string(backend_ports_[i])  +
                        ",\"connections\":" + std::to_string(active_connections_[i]) +
                        ",\"healthy\":"     + (backend_healthy_[i] ? "true" : "false") + "}";
            }
            body += "]}";
        }

        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        send(client, response.data(), response.size(), MSG_NOSIGNAL);
        close(client);
    }

    close(stats_socket_);
    stats_socket_ = -1;
}

void LoadBalancer::stop() {
    if (stop_flag_.exchange(true)) return;
    if (server_socket_ >= 0) { close(server_socket_); server_socket_ = -1; }
    if (stats_socket_  >= 0) { close(stats_socket_);  stats_socket_  = -1; }
    if (health_check_thread_.joinable()) health_check_thread_.join();
    if (stats_thread_.joinable())        stats_thread_.join();
}

void LoadBalancer::start() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        log_err("Failed to create server socket: " + std::string(strerror(errno)));
        return;
    }

    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(listen_port_);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket_, reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) < 0) {
        log_err("Failed to bind on port " + std::to_string(listen_port_) +
                ": " + strerror(errno));
        close(server_socket_);
        return;
    }

    if (listen(server_socket_, SOMAXCONN) < 0) {
        log_err("Failed to listen: " + std::string(strerror(errno)));
        close(server_socket_);
        return;
    }

    health_check_thread_ = std::thread(&LoadBalancer::health_check_loop, this);
    stats_thread_        = std::thread(&LoadBalancer::stats_loop, this);
    log_info("Listening on port " + std::to_string(listen_port_) +
             " with " + std::to_string(backend_ports_.size()) + " backends.");

    while (!stop_flag_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket_,
                                   reinterpret_cast<sockaddr*>(&client_addr),
                                   &client_len);
        if (client_socket < 0) {
            if (stop_flag_) break;
            log_err("accept() failed: " + std::string(strerror(errno)));
            continue;
        }
        thread_pool_.enqueue([this, client_socket] {
            handle_client(client_socket);
        });
    }
}
