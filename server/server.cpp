#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// Minimal single-threaded HTTP/1.0 echo server.
// Run one instance per backend port: ./echo_server 8001
// Responds 200 OK with a JSON body identifying its own port.

int main(int argc, char* argv[]) {
    int port = (argc > 1) ? std::stoi(argv[1]) : 8001;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(server_fd, SOMAXCONN) < 0) { perror("listen"); return 1; }

    std::cout << "Echo server listening on port " << port << "\n";

    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) break;

        char buf[4096];
        ssize_t n = read(client, buf, sizeof(buf) - 1);
        if (n > 0) buf[n] = '\0'; // consume the request (not inspected)

        std::string body   = "{\"port\":" + std::to_string(port) + "}";
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        send(client, response.data(), response.size(), MSG_NOSIGNAL);
        close(client);
    }

    close(server_fd);
    return 0;
}
