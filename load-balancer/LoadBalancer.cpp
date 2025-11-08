#include "LoadBalancer.h"
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <climits>
#include <sys/select.h>
#include <sys/socket.h>
#include <chrono>

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

// Guarantees all bytes are sent; returns false on error.
static bool send_all(int fd, const char* buf, ssize_t len) {
    ssize_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, static_cast<size_t>(len - sent), MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

// Non-blocking TCP probe with 2-second timeout — used for health checks.
static bool tcp_probe(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    bool healthy = false;
    int ret = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (ret == 0) {
        healthy = true;
    } else if (errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);
        struct timeval tv{2, 0};
        if (select(sock + 1, nullptr, &wfds, nullptr, &tv) > 0) {
            int error = 0;
            socklen_t len = sizeof(error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
            healthy = (error == 0);
        }
    }

    close(sock);
    return healthy;
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

// Selects a backend AND increments its counter atomically under one lock,
// eliminating the TOCTOU race of the original two-lock approach.
int LoadBalancer::select_and_reserve_backend() {
    std::lock_guard<std::mutex> lock(connection_mutex_);

    if (algorithm_ == Algorithm::ROUND_ROBIN) {
        for (size_t i = 0; i < backend_ports_.size(); ++i) {
            int idx = (rr_index_++) % static_cast<int>(backend_ports_.size());
            if (backend_healthy_[idx]) {
                active_connections_[idx]++;
                return idx;
            }
        }
        return -1;
    }

    // Least connections: scan all healthy backends for minimum.
    int best = -1;
    int min_conn = INT_MAX;
    for (size_t i = 0; i < backend_ports_.size(); ++i) {
        if (backend_healthy_[i] && active_connections_[i] < min_conn) {
            min_conn = active_connections_[i];
            best = static_cast<int>(i);
        }
    }
    if (best >= 0) active_connections_[best]++;
    return best;
}

void LoadBalancer::handle_client(int client_socket) {
    int index = select_and_reserve_backend();
    if (index < 0) {
        std::cerr << "No healthy backend available — dropping connection.\n";
        close(client_socket);
        return;
    }

    int backend_port = backend_ports_[index];
    int backend_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_socket < 0) {
        std::cerr << "Failed to create backend socket: " << strerror(errno) << "\n";
        std::lock_guard<std::mutex> lock(connection_mutex_);
        active_connections_[index]--;
        close(client_socket);
        return;
    }

    sockaddr_in backend_addr{};
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(backend_port);
    inet_pton(AF_INET, "127.0.0.1", &backend_addr.sin_addr);

    if (connect(backend_socket, reinterpret_cast<sockaddr*>(&backend_addr), sizeof(backend_addr)) < 0) {
        std::cerr << "Failed to connect to backend port " << backend_port
                  << ": " << strerror(errno) << "\n";
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            active_connections_[index]--;
            backend_healthy_[index] = false;
        }
        close(client_socket);
        close(backend_socket);
        return;
    }

    // Bidirectional forwarding via select() — handles both directions concurrently,
    // avoids the deadlock of sequential read/write, and respects the idle timeout.
    char buffer[4096];
    int maxfd = std::max(client_socket, backend_socket) + 1;

    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        FD_SET(backend_socket, &read_fds);
        struct timeval timeout{30, 0}; // 30-second idle timeout

        int ready = select(maxfd, &read_fds, nullptr, nullptr, &timeout);
        if (ready <= 0) break; // timeout or error

        if (FD_ISSET(client_socket, &read_fds)) {
            ssize_t n = read(client_socket, buffer, sizeof(buffer));
            if (n <= 0) break;
            if (!send_all(backend_socket, buffer, n)) break;
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
        remaining = active_connections_[index]; // capture inside lock to avoid data race
    }
    std::cout << "Backend port " << backend_port << " connection closed. Active: " << remaining << "\n";
}

void LoadBalancer::health_check_loop() {
    while (!stop_flag_) {
        for (size_t i = 0; i < backend_ports_.size(); ++i) {
            if (stop_flag_) return;
            bool healthy = tcp_probe(backend_ports_[i]);
            std::lock_guard<std::mutex> lock(connection_mutex_);
            if (backend_healthy_[i] != healthy) {
                std::cout << "[health] Backend port " << backend_ports_[i]
                          << (healthy ? " is UP\n" : " is DOWN\n");
            }
            backend_healthy_[i] = healthy;
        }
        // Sleep in 1-second steps so stop() isn't delayed by the full interval.
        for (int i = 0; i < 5 && !stop_flag_; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void LoadBalancer::stop() {
    if (stop_flag_.exchange(true)) return; // idempotent — safe to call more than once
    if (server_socket_ >= 0) {
        close(server_socket_);
        server_socket_ = -1;
    }
    if (health_check_thread_.joinable())
        health_check_thread_.join();
}

void LoadBalancer::start() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        std::cerr << "Failed to create server socket: " << strerror(errno) << "\n";
        return;
    }

    // Allow immediate restart after crash (avoids TIME_WAIT bind failure).
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(listen_port_);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind on port " << listen_port_ << ": " << strerror(errno) << "\n";
        close(server_socket_);
        return;
    }

    if (listen(server_socket_, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen: " << strerror(errno) << "\n";
        close(server_socket_);
        return;
    }

    health_check_thread_ = std::thread(&LoadBalancer::health_check_loop, this);
    std::cout << "Load balancer listening on port " << listen_port_
              << " with " << backend_ports_.size() << " backends.\n";

    while (!stop_flag_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_socket < 0) {
            if (stop_flag_) break;
            std::cerr << "accept() failed: " << strerror(errno) << "\n";
            continue;
        }
        thread_pool_.enqueue([this, client_socket] {
            handle_client(client_socket);
        });
    }
}
