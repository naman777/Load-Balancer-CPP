#include "LoadBalancer.h"
#include <csignal>
#include <iostream>
#include <vector>

static LoadBalancer* g_lb = nullptr;

static void signal_handler(int) {
    std::cout << "\nShutting down gracefully...\n";
    if (g_lb) g_lb->stop();
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::vector<int> backend_ports = {8001, 8002, 8003};
    LoadBalancer lb(8080, backend_ports, Algorithm::LEAST_CONNECTIONS, 16);
    g_lb = &lb;
    lb.start();
    return 0;
}
