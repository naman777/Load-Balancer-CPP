#include "../load-balancer/LoadBalancer.h"
#include "../load-balancer/backend_selector.h"
#include <atomic>
#include <cassert>
#include <iostream>
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

// ── Round-robin ───────────────────────────────────────────────────────────────

void test_rr_cycles_all_backends() {
    std::vector<bool> healthy = {true, true, true};
    int idx = 0;
    assert(select_round_robin(idx, 3, healthy) == 0);
    assert(select_round_robin(idx, 3, healthy) == 1);
    assert(select_round_robin(idx, 3, healthy) == 2);
    assert(select_round_robin(idx, 3, healthy) == 0); // wraps
    std::cout << "PASS  rr_cycles_all_backends\n";
}

void test_rr_skips_unhealthy() {
    std::vector<bool> healthy = {true, false, true};
    int idx = 0;
    assert(select_round_robin(idx, 3, healthy) == 0);
    assert(select_round_robin(idx, 3, healthy) == 2); // skips index 1
    std::cout << "PASS  rr_skips_unhealthy\n";
}

void test_rr_all_unhealthy() {
    std::vector<bool> healthy = {false, false};
    int idx = 0;
    assert(select_round_robin(idx, 2, healthy) == -1);
    std::cout << "PASS  rr_all_unhealthy\n";
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

// ── ThreadPool ────────────────────────────────────────────────────────────────

void test_threadpool_executes_all_tasks() {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 100; ++i)
            pool.enqueue([&counter] { counter++; });
    }
    assert(counter == 100);
    std::cout << "PASS  threadpool_executes_all_tasks\n";
}

void test_threadpool_concurrent_increments() {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(8);
        for (int i = 0; i < 10000; ++i)
            pool.enqueue([&counter] {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
    }
    assert(counter == 10000);
    std::cout << "PASS  threadpool_concurrent_increments\n";
}

void test_threadpool_indexed_slots() {
    const int N = 200;
    std::vector<std::atomic<int>> slots(N);
    for (auto& s : slots) s.store(0);
    {
        ThreadPool pool(4);
        for (int i = 0; i < N; ++i)
            pool.enqueue([i, &slots] { slots[i].fetch_add(1); });
    }
    for (int i = 0; i < N; ++i) assert(slots[i].load() == 1);
    std::cout << "PASS  threadpool_indexed_slots\n";
}

void test_threadpool_empty_is_safe() {
    ThreadPool pool(4);
    std::cout << "PASS  threadpool_empty_is_safe\n";
}

int main() {
    test_lc_picks_minimum();
    test_lc_skips_unhealthy();
    test_lc_all_unhealthy();
    test_lc_single_backend();

    test_rr_cycles_all_backends();
    test_rr_skips_unhealthy();
    test_rr_all_unhealthy();

    test_ip_hash_deterministic();
    test_ip_hash_skips_unhealthy();
    test_ip_hash_all_unhealthy();

    test_threadpool_executes_all_tasks();
    test_threadpool_concurrent_increments();
    test_threadpool_indexed_slots();
    test_threadpool_empty_is_safe();

    std::cout << "\nAll 14 tests passed.\n";
    return 0;
}
