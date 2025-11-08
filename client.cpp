#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

static constexpr int  DEFAULT_PORT   = 8080;
static constexpr int  BUFFER_SIZE    = 4096;

class Client {
public:
    Client(const std::string& host, int port) : host_(host), port_(port) {}

    ~Client() {
        if (fd_ >= 0) close(fd_);
    }

    void run() {
        connect_to_server();

        std::string message;
        std::cout << "Input: ";
        std::getline(std::cin, message);
        message += "\r\n";

        send_all(message.data(), static_cast<ssize_t>(message.size()));

        std::string response = receive();
        std::cout << "Output: " << response << "\n";
    }

private:
    std::string host_;
    int port_;
    int fd_ = -1;

    void connect_to_server() {
        addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res = nullptr;
        std::string port_str = std::to_string(port_);
        int rc = getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &res);
        if (rc != 0)
            throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rc));

        for (addrinfo* p = res; p; p = p->ai_next) {
            fd_ = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd_ < 0) continue;
            if (connect(fd_, p->ai_addr, p->ai_addrlen) == 0) break;
            close(fd_);
            fd_ = -1;
        }
        freeaddrinfo(res);

        if (fd_ < 0)
            throw std::runtime_error("Failed to connect to " + host_ + ":" + port_str);

        std::cout << "Connected to " << host_ << ":" << port_ << "\n";
    }

    void send_all(const char* buf, ssize_t len) {
        ssize_t sent = 0;
        while (sent < len) {
            ssize_t n = send(fd_, buf + sent, static_cast<size_t>(len - sent), MSG_NOSIGNAL);
            if (n <= 0) throw std::runtime_error("send failed");
            sent += n;
        }
    }

    std::string receive() {
        char buffer[BUFFER_SIZE];
        ssize_t n = read(fd_, buffer, sizeof(buffer) - 1);
        if (n < 0) throw std::runtime_error("read failed");
        buffer[n] = '\0';
        return std::string(buffer);
    }
};

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = DEFAULT_PORT;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::atoi(argv[2]);

    try {
        Client client(host, port);
        client.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
