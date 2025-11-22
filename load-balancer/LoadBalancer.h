#ifndef LOADBALANCER_H
#define LOADBALANCER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

enum class Algorithm {
    LEAST_CONNECTIONS,
    ROUND_ROBIN,
    IP_HASH
};

class ThreadPool {
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool();
    void enqueue(std::function<void()> task);

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

class LoadBalancer {
public:
    LoadBalancer(int listen_port, const std::vector<int>& ports,
                 Algorithm algo           = Algorithm::LEAST_CONNECTIONS,
                 size_t thread_pool_size  = 16,
                 std::vector<int> weights = {},
                 int max_conn             = 0);
    ~LoadBalancer();
    void start();
    void stop();

    // Runtime-updatable settings (safe to call from any thread).
    void set_algorithm(Algorithm a);
    void set_weights(const std::vector<int>& w); // rebuilds weighted-RR sequence

private:
    int listen_port_;
    std::vector<int> backend_ports_;
    std::vector<int> backend_weights_;
    std::vector<int> rr_sequence_;      // pre-expanded for weighted round-robin
    std::vector<int> active_connections_;
    std::vector<bool> backend_healthy_;
    std::mutex connection_mutex_;
    std::atomic<bool> stop_flag_{false};
    int rr_index_              = 0;
    int max_conn_per_backend_  = 0;     // 0 = unlimited
    std::atomic<Algorithm> algorithm_;
    ThreadPool thread_pool_;
    std::thread health_check_thread_;
    std::thread stats_thread_;
    int server_socket_ = -1;
    int stats_socket_  = -1;

    void rebuild_rr_sequence(); // must be called under connection_mutex_
    int  select_and_reserve_backend(uint32_t client_ip = 0);
    void handle_client(int client_socket);
    void health_check_loop();
    void stats_loop();
};

#endif // LOADBALANCER_H
