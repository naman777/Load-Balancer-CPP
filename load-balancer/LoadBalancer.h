#ifndef LOADBALANCER_H
#define LOADBALANCER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

enum class Algorithm {
    LEAST_CONNECTIONS,
    ROUND_ROBIN,
    IP_HASH,
    RENDEZVOUS  // consistent hashing — minimal remapping on topology changes
};

// Event-driven TCP/HTTP load balancer, modelled on nginx's worker design:
// `worker_count` threads each run their own epoll loop over their own
// SO_REUSEPORT listening socket, and every socket is non-blocking. A worker
// can therefore hold thousands of connections at once, instead of one
// connection per thread as with the old fixed-size thread pool.
class LoadBalancer {
public:
    LoadBalancer(int listen_port, const std::vector<int>& ports,
                 Algorithm algo           = Algorithm::LEAST_CONNECTIONS,
                 size_t worker_count      = 0,   // 0 = one per CPU core
                 std::vector<int> weights = {},
                 int max_conn             = 0);
    ~LoadBalancer();
    void start(); // blocks until stop() is called
    void stop();

    // Runtime-updatable settings (safe to call from any thread).
    void set_algorithm(Algorithm a);
    void set_weights(const std::vector<int>& w); // rebuilds weighted-RR sequence

private:
    struct Worker; // one epoll event loop; defined in LoadBalancer.cpp

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
    size_t worker_count_;
    std::mutex lifecycle_mutex_;        // guards workers_ between start() and stop()
    std::vector<std::unique_ptr<Worker>> workers_;
    std::thread health_check_thread_;
    std::thread stats_thread_;
    int stats_socket_  = -1;

    void rebuild_rr_sequence(); // must be called under connection_mutex_
    int  select_and_reserve_backend(uint32_t client_ip = 0);
    void release_backend(int index, bool mark_unhealthy = false);
    int  open_listen_socket();
    void health_check_loop();
    void stats_loop();
};

#endif // LOADBALANCER_H
