#ifndef LOADBALANCER_H
#define LOADBALANCER_H

#include <vector>
#include <thread>
#include <mutex>

class LoadBalancer {
private:
    std::vector<int> backend_ports;
    std::vector<int> active_connections;
    std::mutex connection_mutex;
    int listen_port;

    int find_least_connection_index(); // Finds the index of the backend port with the least active connections
    void handle_client(int client_socket); // Handles a client connection

public:
    LoadBalancer(int listen_port, const std::vector<int>& ports);
    void start(); // Start the load balancer server
};

#endif // LOADBALANCER_H