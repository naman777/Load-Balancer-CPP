#pragma once
#include <climits>
#include <cstdint>
#include <vector>

// Pure, side-effect-free backend selection functions.
// Callers must hold connection_mutex_ before invoking any of these.
// Returning -1 means no healthy backend is available.

inline int select_least_connections(const std::vector<int>& conns,
                                    const std::vector<bool>& healthy) {
    int best = -1, min_c = INT_MAX;
    for (int i = 0; i < static_cast<int>(conns.size()); ++i) {
        if (healthy[i] && conns[i] < min_c) { min_c = conns[i]; best = i; }
    }
    return best;
}

inline int select_round_robin(int& index, size_t n,
                              const std::vector<bool>& healthy) {
    for (size_t i = 0; i < n; ++i) {
        int idx = index++ % static_cast<int>(n);
        if (healthy[idx]) return idx;
    }
    return -1;
}

// Hashes client_ip to a starting backend; walks forward to find a healthy one.
inline int select_ip_hash(uint32_t client_ip, const std::vector<bool>& healthy) {
    const size_t n = healthy.size();
    for (size_t i = 0; i < n; ++i) {
        int idx = static_cast<int>((client_ip + i) % n);
        if (healthy[idx]) return idx;
    }
    return -1;
}
