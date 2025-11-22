#pragma once
#include <climits>
#include <cstdint>
#include <limits>
#include <vector>

// Pure, side-effect-free backend selection functions.
// Callers must hold connection_mutex_ before invoking any of these.
// max_conn: per-backend connection cap; 0 = unlimited.
// Returns -1 when no healthy, uncapped backend is available.

inline int select_least_connections(const std::vector<int>& conns,
                                    const std::vector<bool>& healthy,
                                    int max_conn = 0) {
    int best = -1, min_c = INT_MAX;
    for (int i = 0; i < static_cast<int>(conns.size()); ++i) {
        if (!healthy[i]) continue;
        if (max_conn > 0 && conns[i] >= max_conn) continue;
        if (conns[i] < min_c) { min_c = conns[i]; best = i; }
    }
    return best;
}

// Weighted least-connections: pick backend with minimum connections/weight.
// A backend with weight=2 absorbs twice as many connections before being
// considered "busier" than a weight=1 backend.
inline int select_weighted_lc(const std::vector<int>& conns,
                               const std::vector<int>& weights,
                               const std::vector<bool>& healthy,
                               int max_conn = 0) {
    int best = -1;
    double min_ratio = std::numeric_limits<double>::max();
    for (int i = 0; i < static_cast<int>(conns.size()); ++i) {
        if (!healthy[i] || weights[i] <= 0) continue;
        if (max_conn > 0 && conns[i] >= max_conn) continue;
        double ratio = static_cast<double>(conns[i]) / weights[i];
        if (ratio < min_ratio) { min_ratio = ratio; best = i; }
    }
    return best;
}

inline int select_round_robin(int& index, size_t n,
                               const std::vector<bool>& healthy,
                               int max_conn = 0,
                               const std::vector<int>* conns = nullptr) {
    for (size_t i = 0; i < n; ++i) {
        int idx = index++ % static_cast<int>(n);
        if (!healthy[idx]) continue;
        if (max_conn > 0 && conns && (*conns)[idx] >= max_conn) continue;
        return idx;
    }
    return -1;
}

// Weighted round-robin using a pre-expanded sequence.
// sequence is built from weights: weights [2,1] → [0,0,1].
// Cycles through sequence, skipping unhealthy or capped backends.
inline int select_weighted_rr(int& index,
                               const std::vector<int>& sequence,
                               const std::vector<int>& conns,
                               const std::vector<bool>& healthy,
                               int max_conn = 0) {
    const int n = static_cast<int>(sequence.size());
    if (n == 0) return -1;
    for (int i = 0; i < n; ++i) {
        int backend = sequence[index++ % n];
        if (!healthy[backend]) continue;
        if (max_conn > 0 && conns[backend] >= max_conn) continue;
        return backend;
    }
    return -1;
}

// FNV-1a 32-bit hash — better avalanche than plain modulo.
inline uint32_t fnv1a(uint32_t ip) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 4; ++i) { h ^= (ip >> (8 * i)) & 0xFFu; h *= 16777619u; }
    return h;
}

// Hashes client IP with FNV-1a; walks forward to find a healthy, uncapped backend.
inline int select_ip_hash(uint32_t client_ip, const std::vector<bool>& healthy,
                           int max_conn = 0,
                           const std::vector<int>* conns = nullptr) {
    const size_t n = healthy.size();
    uint32_t h = fnv1a(client_ip);
    for (size_t i = 0; i < n; ++i) {
        int idx = static_cast<int>((h + i) % n);
        if (!healthy[idx]) continue;
        if (max_conn > 0 && conns && (*conns)[idx] >= max_conn) continue;
        return idx;
    }
    return -1;
}

// Rendezvous (Highest Random Weight) hashing.
//
// Each backend gets a score = fnv1a(client_ip XOR backend_salt).
// The backend with the highest score wins. When a backend is added or
// removed, only the ~1/n fraction of clients mapped to that backend
// get remapped — far less disruption than fnv1a(ip) % n (which remaps
// nearly all clients on any topology change).
inline uint32_t rendezvous_score(uint32_t client_ip, int backend_idx) {
    // Knuth multiplicative hash mixes the backend index before XOR so
    // adjacent indices produce very different salts.
    uint32_t salt = static_cast<uint32_t>(backend_idx) * 2654435761u;
    return fnv1a(client_ip ^ salt);
}

inline int select_rendezvous(uint32_t client_ip, const std::vector<bool>& healthy,
                              int max_conn = 0,
                              const std::vector<int>* conns = nullptr) {
    int best = -1;
    uint32_t best_score = 0;
    for (int i = 0; i < static_cast<int>(healthy.size()); ++i) {
        if (!healthy[i]) continue;
        if (max_conn > 0 && conns && (*conns)[i] >= max_conn) continue;
        uint32_t score = rendezvous_score(client_ip, i);
        if (best < 0 || score > best_score) { best_score = score; best = i; }
    }
    return best;
}
