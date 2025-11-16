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

// FNV-1a 32-bit hash — better avalanche than plain modulo.
inline uint32_t fnv1a(uint32_t ip) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 4; ++i) { h ^= (ip >> (8 * i)) & 0xFFu; h *= 16777619u; }
    return h;
}

// Hashes client IP with FNV-1a; walks forward to find a healthy backend.
inline int select_ip_hash(uint32_t client_ip, const std::vector<bool>& healthy) {
    const size_t n = healthy.size();
    uint32_t h = fnv1a(client_ip);
    for (size_t i = 0; i < n; ++i) {
        int idx = static_cast<int>((h + i) % n);
        if (healthy[idx]) return idx;
    }
    return -1;
}
