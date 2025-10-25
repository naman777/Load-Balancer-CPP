#include "LoadBalancer.h"
#include <vector>

int main() {
    std::vector<int> backend_ports = {8001, 8002, 8003};
    LoadBalancer lb(8080, backend_ports);
    lb.start();
    return 0;
}