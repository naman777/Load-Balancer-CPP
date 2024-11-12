#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define SERVERPORT 6000
#define BUFFER_SIZE 100

class Client {
private:
    std::string serverName;
    int serverPort;
    int socketFd;
    struct sockaddr_in serverAddress;
    struct hostent* serverHost;

    void initializeSocket() {
        // Create TCP socket
        socketFd = socket(AF_INET, SOCK_STREAM, 0);
        if (socketFd == -1) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }
    

    void resolveServerHost() {
        // Resolve server name to host address
        serverHost = gethostbyname(serverName.c_str());
        if (!serverHost) {
            std::cerr << "Failed to resolve host: " << serverName << "\n";
            exit(EXIT_FAILURE);
        }

        // Initialize server address structure
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(serverPort);
        memcpy(&serverAddress.sin_addr.s_addr, serverHost->h_addr, serverHost->h_length);
    }

    void connectToServer() {
        if (connect(socketFd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1) {
            perror("Connection to server failed");
            exit(EXIT_FAILURE);
        }
        std::cout << "Connection established with server.\n";
    }

    void sendMessage(const std::string& message) {
        if (write(socketFd, message.c_str(), message.length()) == -1) {
            perror("Write to server failed");
            exit(EXIT_FAILURE);
        }
    }

    std::string receiveMessage() {
        char buffer[BUFFER_SIZE] = {0};
        if (read(socketFd, buffer, BUFFER_SIZE) == -1) {
            perror("Read from server failed");
            exit(EXIT_FAILURE);
        }
        return std::string(buffer);
    }

public:
    Client(const std::string& serverName, int serverPort)
        : serverName(serverName), serverPort(serverPort), socketFd(-1) {}

    ~Client() {
        if (socketFd != -1) {
            close(socketFd);
        }
    }

    void run() {
        initializeSocket();
        resolveServerHost();
        connectToServer();

        // Interact with the server
        std::string inputMessage;
        std::cout << "Input: ";
        std::cin >> inputMessage;

        sendMessage(inputMessage);
        std::string response = receiveMessage();
        std::cout << "Output: " << response << "\n";
    }
};

// Main Function
int main(int argc, char* argv[]) {
    // Set default server name and port
    std::string serverName = "127.0.0.1";
    int serverPort = SERVERPORT;

    // Optionally override server name and port from command-line arguments
    if (argc > 1) {
        serverName = argv[1];
    }
    if (argc > 2) {
        serverPort = std::atoi(argv[2]);
    }

    try {
        Client client(serverName, serverPort);
        client.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
