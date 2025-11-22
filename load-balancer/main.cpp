#include "LoadBalancer.h"
#include "config.h"
#include "logger.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static LoadBalancer* g_lb = nullptr;

static void signal_handler(int) {
    log_info("Shutting down gracefully...");
    if (g_lb) g_lb->stop();
}

static std::vector<int> parse_ports(const std::string& s) {
    std::vector<int> ports;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ','))
        ports.push_back(std::stoi(token));
    return ports;
}

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "  --config <file>     Load settings from config file\n"
              << "  --port <n>          Listen port (default: 8080)\n"
              << "  --backends <p,...>  Comma-separated backend ports (default: 8001,8002,8003)\n"
              << "  --weights <w,...>   Per-backend weights, same order as --backends\n"
              << "  --algo <lc|rr|ih>   Algorithm: least-conn | round-robin | ip-hash (default: lc)\n"
              << "  --threads <n>       Thread pool size (default: 16)\n"
              << "  --max-conn <n>      Max concurrent connections per backend; 0=unlimited (default: 0)\n"
              << "  --help              Show this message\n"
              << "\nSend SIGHUP to reload algo and weights from the config file without restarting.\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    LBConfig cfg;
    std::string config_path;

    // First pass: find --config and --help.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
        if (arg == "--config" && i + 1 < argc) config_path = argv[++i];
    }

    if (!config_path.empty()) {
        try { cfg = load_config(config_path); }
        catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    }

    // Second pass: CLI flags override config file values.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config") { ++i; continue; }
        else if (arg == "--port"     && i + 1 < argc) cfg.port    = std::stoi(argv[++i]);
        else if (arg == "--backends" && i + 1 < argc) cfg.backends = parse_ports(argv[++i]);
        else if (arg == "--weights"  && i + 1 < argc) cfg.weights  = parse_ports(argv[++i]);
        else if (arg == "--threads"  && i + 1 < argc) cfg.threads   = static_cast<size_t>(std::stoi(argv[++i]));
        else if (arg == "--max-conn" && i + 1 < argc) cfg.max_conn  = std::stoi(argv[++i]);
        else if (arg == "--algo"     && i + 1 < argc) {
            std::string a = argv[++i];
            if      (a == "rr") cfg.algo = Algorithm::ROUND_ROBIN;
            else if (a == "ih") cfg.algo = Algorithm::IP_HASH;
            else if (a == "lc") cfg.algo = Algorithm::LEAST_CONNECTIONS;
            else { std::cerr << "Unknown algo: " << a << "\n"; return 1; }
        } else if (arg != "--help" && arg != "-h") {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (cfg.backends.empty()) { std::cerr << "At least one backend is required.\n"; return 1; }

    LoadBalancer lb(cfg.port, cfg.backends, cfg.algo, cfg.threads,
                    cfg.weights, cfg.max_conn);
    g_lb = &lb;

    // SIGHUP: reload algorithm and weights from config file without restarting.
    // SIGHUP is blocked in all threads and consumed exclusively via sigwait() —
    // the only signal-safe way to call arbitrary C++ from a signal context.
    if (!config_path.empty()) {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGHUP);
        pthread_sigmask(SIG_BLOCK, &mask, nullptr);

        std::vector<int> initial_backends = cfg.backends;

        std::thread([mask, config_path, &lb, initial_backends] {
            sigset_t m = mask;
            while (true) {
                int sig;
                if (sigwait(&m, &sig) != 0 || sig != SIGHUP) break;
                try {
                    auto nc = load_config(config_path);
                    lb.set_algorithm(nc.algo);
                    if (!nc.weights.empty()) lb.set_weights(nc.weights);
                    if (nc.backends != initial_backends)
                        log_warn("SIGHUP: backend list changes require restart — ignored.");
                    log_info("SIGHUP: config reloaded from " + config_path);
                } catch (const std::exception& e) {
                    log_err("SIGHUP reload failed: " + std::string(e.what()));
                }
            }
        }).detach();
    }

    lb.start();
    return 0;
}
