#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>

#define DEFAULT_PORT 1601
#define BUFFER_SIZE 1024

void handleError(const std::string &message) {
    std::cerr << message << ": " << strerror(errno) << std::endl;
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc == 2) {
        port = std::stoi(argv[1]);
    } else {
        std::cerr << "Using default port: " << DEFAULT_PORT << std::endl;
    }

    int listensock;
    sockaddr_in rAddr{}, sAddr{};
    socklen_t addrlen = sizeof(rAddr);

    // Create socket
    listensock = socket(AF_INET, SOCK_DGRAM, 0);
    if (listensock < 0) {
        handleError("Socket creation failed");
    }

    // Set socket options
    int reuse = 1;
    if (setsockopt(listensock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        handleError("Failed to set SO_REUSEADDR");
    }

    int broadcastEnable = 1;
    if (setsockopt(listensock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        handleError("Failed to enable broadcast");
    }

    // Bind socket
    rAddr.sin_family = AF_INET;
    rAddr.sin_port = htons(port);
    rAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listensock, reinterpret_cast<struct sockaddr*>(&rAddr), sizeof(rAddr)) < 0) {
        handleError("Error binding socket");
    }

    // Setup broadcast address
    sAddr.sin_family = AF_INET;
    sAddr.sin_port = htons(port);
    sAddr.sin_addr.s_addr = inet_addr("255.255.255.255");

    int clientID = getpid();
    fd_set readfds, tmpfds;
    char buffer[BUFFER_SIZE];
    FD_ZERO(&readfds);
    FD_SET(listensock, &readfds);
    FD_SET(STDIN_FILENO, &readfds);
    int max_fd = std::max(listensock, STDIN_FILENO) + 1;

    while (true) {
        tmpfds = readfds;
        int result = select(max_fd, &tmpfds, nullptr, nullptr, nullptr);
        if (result < 0) {
            handleError("Failure in select");
        }

        for (int i = 0; i < max_fd; ++i) {
            if (FD_ISSET(i, &tmpfds)) {
                if (i == listensock) {
                    memset(buffer, 0, sizeof(buffer));
                    int bytesReceived = recvfrom(listensock, buffer, sizeof(buffer) - 1, 0,
                                                 reinterpret_cast<struct sockaddr*>(&rAddr), &addrlen);
                    if (bytesReceived > 0) {
                        buffer[bytesReceived] = '\0';
                        int receivedID;
                        std::string messageContent(buffer);
                        if (sscanf(buffer, "User ID: %d\n", &receivedID) == 1) {
                            if (receivedID != clientID) {
                                std::cout << "Received from " << receivedID << ":\n" << messageContent.substr(messageContent.find('\n') + 1) << std::endl;
                            }
                        }
                    } else {
                        handleError("Error receiving data");
                    }
                } else if (i == STDIN_FILENO) {
                    memset(buffer, 0, sizeof(buffer));
                    if (fgets(buffer, sizeof(buffer) - 1, stdin) != nullptr) {
                        std::string message = "User ID: " + std::to_string(clientID) + "\n" + buffer;
                        int bytesSent = sendto(listensock, message.c_str(), message.size(), 0,
                                               reinterpret_cast<struct sockaddr*>(&sAddr), sizeof(sAddr));
                        if (bytesSent < 0) {
                            handleError("Error sending data");
                        }

                    }
                }
            }
        }
    }

    close(listensock);
    return 0;
}