#include "LoadBalancer.h"
#include <csignal>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdlib>

static LoadBalancer* g_lb = nullptr;

static void signal_handler(int) {
    std::cout << "\nShutting down gracefully...\n";
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
              << "  --port <n>          Listen port (default: 8080)\n"
              << "  --backends <p,...>  Comma-separated backend ports (default: 8001,8002,8003)\n"
              << "  --algo <lc|rr|ih>   Algorithm: least-conn, round-robin, ip-hash (default: lc)\n"
              << "  --threads <n>       Thread pool size (default: 16)\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int port = 8080;
    std::vector<int> backends = {8001, 8002, 8003};
    Algorithm algo = Algorithm::LEAST_CONNECTIONS;
    size_t threads = 16;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--help" || arg == "-h")) {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--backends" && i + 1 < argc) {
            backends = parse_ports(argv[++i]);
        } else if (arg == "--algo" && i + 1 < argc) {
            std::string a = argv[++i];
            if (a == "rr")      algo = Algorithm::ROUND_ROBIN;
            else if (a == "ih") algo = Algorithm::IP_HASH;
            else if (a != "lc") { std::cerr << "Unknown algo: " << a << "\n"; return 1; }
        } else if (arg == "--threads" && i + 1 < argc) {
            threads = static_cast<size_t>(std::stoi(argv[++i]));
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (backends.empty()) {
        std::cerr << "At least one backend port is required.\n";
        return 1;
    }

    LoadBalancer lb(port, backends, algo, threads);
    g_lb = &lb;
    lb.start();
    return 0;
}
