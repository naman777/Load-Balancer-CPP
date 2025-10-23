#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BACKLOG 10

int clientNumber = 0;
int numberOfConnections = 0;

struct pthread_arg_t {
    int new_socket_fd;
    sockaddr_in client_address;
    // Additional arguments can be added here if needed
};

/* Function declarations */
void *pthread_routine(void *arg);
void signal_handler(int signal_number);
void upper_string(char s[]);

int main(int argc, char *argv[]) {
    int port, socket_fd, new_socket_fd;
    sockaddr_in address;
    pthread_attr_t pthread_attr;
    pthread_arg_t *pthread_arg;
    pthread_t pthread;
    socklen_t client_address_len;

    /* Get port from command line arguments or stdin. */
    if (argc > 1) {
        port = std::atoi(argv[1]);
    } else {
        std::cout << "Enter Port: ";
        std::cin >> port;
    }

    /* Initialize IPv4 address. */
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    /* Create TCP socket. */
    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    /* Bind address to socket. */
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind");
        exit(1);
    }

    /* Listen on socket. */
    if (listen(socket_fd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    /* Assign signal handlers to signals. */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        exit(1);
    }
    if (signal(SIGTERM, signal_handler) == SIG_ERR) {
        perror("signal");
        exit(1);
    }
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        perror("signal");
        exit(1);
    }

    /* Initialize pthread attribute to create detached threads. */
    if (pthread_attr_init(&pthread_attr) != 0) {
        perror("pthread_attr_init");
        exit(1);
    }
    if (pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED) != 0) {
        perror("pthread_attr_setdetachstate");
        exit(1);
    }

    while (true) {
        pthread_arg = new pthread_arg_t();
        if (!pthread_arg) {
            perror("malloc");
            continue;
        }

        /* Accept connection to client. */
        client_address_len = sizeof(pthread_arg->client_address);
        new_socket_fd = accept(socket_fd, (struct sockaddr *)&pthread_arg->client_address, &client_address_len);
        if (new_socket_fd == -1) {
            perror("accept");
            delete pthread_arg;
            continue;
        }

        /* Initialize pthread argument. */
        pthread_arg->new_socket_fd = new_socket_fd;

        /* Create thread to serve connection to client. */
        if (pthread_create(&pthread, &pthread_attr, pthread_routine, (void *)pthread_arg) != 0) {
            perror("pthread_create");
            delete pthread_arg;
            continue;
        }
    }

    return 0;
}

/* Function to convert string to uppercase */
void upper_string(char s[]) {
    int c = 0;
    while (s[c] != '\0') {
        if (s[c] >= 'a' && s[c] <= 'z')
            s[c] = s[c] - 32;
        c++;
    }

    // Loop to increase time complexity
    for (int i = 0; i < 1000000000; i++)
        for (int j = 0; j < 10; j++)
            c = j;
}

/* Thread routine to handle client requests */
void *pthread_routine(void *arg) {
    clientNumber++;
    int clientIdx = clientNumber;
    numberOfConnections++;
    pthread_arg_t *pthread_arg = (pthread_arg_t *)arg;
    int new_socket_fd = pthread_arg->new_socket_fd;

    delete pthread_arg;

    char string[100];
    read(new_socket_fd, string, 100);

    if (std::strcmp("__clients?", string) == 0) {
        std::cout << "Load check\n";
        char result[100];
        sprintf(result, "%d", numberOfConnections - 1);
        write(new_socket_fd, result, 100);
        clientNumber--;
    } else {
        std::cout << "client#" << clientIdx << " : connected\n";
        upper_string(string);
        write(new_socket_fd, string, 100);
        std::cout << "client#" << clientIdx << " : disconnected\n";
    }

    close(new_socket_fd);
    numberOfConnections--;
    return nullptr;
}

/* Signal handler for termination signals */
void signal_handler(int signal_number) {
    exit(0);
}
