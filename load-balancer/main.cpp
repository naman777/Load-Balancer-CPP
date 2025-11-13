#include "LoadBalancer.h"
#include "config.h"
#include "logger.h"
#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

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
              << "  --config <file>     Load settings from config file (default: lb.conf)\n"
              << "  --port <n>          Listen port (default: 8080)\n"
              << "  --backends <p,...>  Comma-separated backend ports (default: 8001,8002,8003)\n"
              << "  --algo <lc|rr|ih>   Algorithm: least-conn | round-robin | ip-hash (default: lc)\n"
              << "  --threads <n>       Thread pool size (default: 16)\n"
              << "  --help              Show this message\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Defaults — config file overrides, then CLI flags override config.
    LBConfig cfg;
    std::string config_path;

    // First pass: check for --config and --help before parsing the rest.
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
        if (arg == "--config") { ++i; continue; } // already handled
        else if (arg == "--port" && i + 1 < argc) {
            cfg.port = std::stoi(argv[++i]);
        } else if (arg == "--backends" && i + 1 < argc) {
            cfg.backends = parse_ports(argv[++i]);
        } else if (arg == "--algo" && i + 1 < argc) {
            std::string a = argv[++i];
            if      (a == "rr") cfg.algo = Algorithm::ROUND_ROBIN;
            else if (a == "ih") cfg.algo = Algorithm::IP_HASH;
            else if (a == "lc") cfg.algo = Algorithm::LEAST_CONNECTIONS;
            else { std::cerr << "Unknown algo: " << a << "\n"; return 1; }
        } else if (arg == "--threads" && i + 1 < argc) {
            cfg.threads = static_cast<size_t>(std::stoi(argv[++i]));
        } else if (arg != "--help" && arg != "-h") {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (cfg.backends.empty()) { std::cerr << "At least one backend is required.\n"; return 1; }

    LoadBalancer lb(cfg.port, cfg.backends, cfg.algo, cfg.threads);
    g_lb = &lb;
    lb.start();
    return 0;
}
