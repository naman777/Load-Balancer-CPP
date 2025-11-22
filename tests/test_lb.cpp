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

    test_threadpool_executes_all_tasks();
    test_threadpool_concurrent_increments();
    test_threadpool_indexed_slots();
    test_threadpool_empty_is_safe();

    std::cout << "\nAll 22 tests passed.\n";
    return 0;
}
