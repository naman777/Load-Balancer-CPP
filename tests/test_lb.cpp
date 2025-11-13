#include "../load-balancer/LoadBalancer.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// ── ThreadPool tests ──────────────────────────────────────────────────────────

void test_threadpool_executes_all_tasks() {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 100; ++i)
            pool.enqueue([&counter] { counter++; });
    } // destructor joins all workers — all tasks must be done by now
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

void test_threadpool_order_independent() {
    // Tasks that write into indexed slots — verify no slot is written twice.
    const int N = 200;
    std::vector<std::atomic<int>> slots(N);
    for (auto& s : slots) s.store(0);
    {
        ThreadPool pool(4);
        for (int i = 0; i < N; ++i)
            pool.enqueue([i, &slots] { slots[i].fetch_add(1); });
    }
    for (int i = 0; i < N; ++i)
        assert(slots[i].load() == 1);
    std::cout << "PASS  threadpool_order_independent\n";
}

void test_threadpool_single_thread() {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(1);
        for (int i = 0; i < 50; ++i)
            pool.enqueue([&counter] { counter++; });
    }
    assert(counter == 50);
    std::cout << "PASS  threadpool_single_thread\n";
}

void test_threadpool_empty_is_safe() {
    ThreadPool pool(4); // enqueue nothing — destructor must not hang
    std::cout << "PASS  threadpool_empty_is_safe\n";
}

int main() {
    test_threadpool_executes_all_tasks();
    test_threadpool_concurrent_increments();
    test_threadpool_order_independent();
    test_threadpool_single_thread();
    test_threadpool_empty_is_safe();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
