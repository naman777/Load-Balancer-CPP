#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BACKLOG 10
#define SERVER_NAME_LEN_MAX 255

#define SERVERPORT 6000
#define SERVER1PORT 6001
#define SERVER2PORT 6002

std::atomic<int> clientNumber = 0; // Atomic to handle concurrency
std::mutex coutMutex; // To synchronize console output

// Encapsulates server logic
class Server {
private:
    int port;
    int serverSocket;

public:
    Server(int port) : port(port), serverSocket(-1) {}

    void start() {
        struct sockaddr_in address = {};
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == -1) {
            perror("socket");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = INADDR_ANY;

        if (bind(serverSocket, (struct sockaddr*)&address, sizeof(address)) == -1) {
            perror("bind");
            exit(EXIT_FAILURE);
        }

        if (listen(serverSocket, BACKLOG) == -1) {
            perror("listen");
            exit(EXIT_FAILURE);
        }

        std::cout << "Server started on port " << port << std::endl;
        signal(SIGINT, Server::signalHandler);

        while (true) {
            struct sockaddr_in clientAddress;
            socklen_t clientLen = sizeof(clientAddress);
            int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientLen);
            if (clientSocket == -1) {
                perror("accept");
                continue;
            }

            clientNumber++;
            std::thread(&Server::handleClient, this, clientSocket, clientNumber.load()).detach();
        }
    }

    static void signalHandler(int signal) {
        std::cout << "\nServer shutting down..." << std::endl;
        exit(0);
    }

    void handleClient(int clientSocket, int clientIdx) {
        {
            std::lock_guard<std::mutex> lock(coutMutex);
            std::cout << "Client #" << clientIdx << " connected." << std::endl;
        }

        char input[100];
        read(clientSocket, input, sizeof(input));

        int load1 = getServerLoad(1);
        int load2 = getServerLoad(2);

        {
            std::lock_guard<std::mutex> lock(coutMutex);
            std::cout << "LOAD:- SERVER1: " << load1 << " SERVER2: " << load2 << std::endl;
        }

        char reply[100];
        if (load1 <= load2) {
            sendQuery(1, input, reply);
        } else {
            sendQuery(2, input, reply);
        }

        write(clientSocket, reply, sizeof(reply));

        {
            std::lock_guard<std::mutex> lock(coutMutex);
            std::cout << "Client #" << clientIdx << " disconnected." << std::endl;
        }

        close(clientSocket);
    }

    static int getServerLoad(int serverIdx) {
        int serverPort = (serverIdx == 1) ? SERVER1PORT : SERVER2PORT;
        int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd == -1) {
            perror("socket");
            return -1;
        }

        struct sockaddr_in server_address = {};
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(serverPort);

        struct hostent* server_host = gethostbyname("localhost");
        if (!server_host) {
            perror("gethostbyname");
            close(socket_fd);
            return -1;
        }

        memcpy(&server_address.sin_addr.s_addr, server_host->h_addr, server_host->h_length);

        if (connect(socket_fd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
            perror("connect");
            close(socket_fd);
            return -1;
        }

        const char* query = "__clients?";
        write(socket_fd, query, strlen(query));

        char reply[100];
        read(socket_fd, reply, sizeof(reply));
        close(socket_fd);

        int load = 0;
        sscanf(reply, "%d", &load);
        return load;
    }

    static void sendQuery(int serverIdx, const char* input, char* reply) {
        int serverPort = (serverIdx == 1) ? SERVER1PORT : SERVER2PORT;
        int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd == -1) {
            perror("socket");
            return;
        }

        struct sockaddr_in server_address = {};
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(serverPort);

        struct hostent* server_host = gethostbyname("localhost");
        if (!server_host) {
            perror("gethostbyname");
            close(socket_fd);
            return;
        }

        memcpy(&server_address.sin_addr.s_addr, server_host->h_addr, server_host->h_length);

        if (connect(socket_fd, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
            perror("connect");
            close(socket_fd);
            return;
        }

        write(socket_fd, input, strlen(input));
        read(socket_fd, reply, 100);
        close(socket_fd);
    }
};

int main() {
    Server server(SERVERPORT);
    server.start();
    return 0;
}
