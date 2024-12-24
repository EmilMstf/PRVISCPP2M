#include <csignal>
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
#include <sstream>
#include <vector>
#include <array>

#define DEFAULT_PORT 1601
#define BUFFER_SIZE 1024
#define COMMAND_TIMEOUT 3 // Таймаут на выполнение команды в секундах

volatile sig_atomic_t timeoutOccurred = 0;

void handleError(const std::string &message) {
    std::cerr << message << ": " << strerror(errno) << std::endl;
    exit(EXIT_FAILURE);
}

void timeoutHandler(int signum) {
    timeoutOccurred = 1;
}

// Функция для формирования консольной команды из сообщения
std::string formCommandFromMessage(const std::string &message) {
    std::istringstream iss(message);
    std::string command;
    std::string token;
    std::vector<std::string> tokens;

    // Разделение сообщения на токены по пробелам
    while (iss >> token) {
        tokens.push_back(token);
    }

    // Формирование команды из токенов
    for (const auto &t : tokens) {
        command += t + " ";
    }

    // Удаление последнего пробела
    if (!command.empty()) {
        command.pop_back();
    }

    return command;
}

// Функция для выполнения команды и получения результата
std::string executeCommand(const std::string &command) {
    std::array<char, BUFFER_SIZE> buffer;
    std::string result;
    std::string fullCommand = command + " 2>&1"; // Перенаправление stderr в stdout

    // Установка обработчика сигнала для таймаута
    signal(SIGALRM, timeoutHandler);
    alarm(COMMAND_TIMEOUT); // Установка таймаута

    FILE* pipe = popen(fullCommand.c_str(), "r");
    if (!pipe) {
        return "popen failed!";
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    int returnCode = pclose(pipe);

    // Отключение таймаута
    alarm(0);

    if (timeoutOccurred) {
        result = "Command execution timed out";
        timeoutOccurred = 0; // Сброс состояния таймаута
    } else if (returnCode != 0) {
        result += "Command execution failed with code " + std::to_string(returnCode);
    }
    return result;
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

     // Создание сокета
    listensock = socket(AF_INET, SOCK_DGRAM, 0);
    if (listensock < 0) {
        handleError("Socket creation failed");
    }

     // Установка опций сокета
    int reuse = 1;
    if (setsockopt(listensock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        handleError("Failed to set SO_REUSEADDR");
    }

    int broadcastEnable = 1;
    if (setsockopt(listensock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)) < 0) {
        handleError("Failed to enable broadcast");
    }

     // Привязка сокета
    rAddr.sin_family = AF_INET;
    rAddr.sin_port = htons(port);
    rAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listensock, reinterpret_cast<struct sockaddr*>(&rAddr), sizeof(rAddr)) < 0) {
        handleError("Error binding socket");
    }

    // Настройка широковещательного адреса
    sAddr.sin_family = AF_INET;
    sAddr.sin_port = htons(port);
    sAddr.sin_addr.s_addr = inet_addr("255.255.255.255");

    int clientID = getpid();
    fd_set readfds, tmpfds;
    char buffer[BUFFER_SIZE];
    FD_ZERO(&readfds);
    FD_SET(listensock, &readfds);
    int max_fd = listensock + 1;

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
                        char messageContent[BUFFER_SIZE];
                        if (sscanf(buffer, "User ID: %d\n%[^\n]", &receivedID, messageContent) == 2) {
                            if (receivedID != clientID) {
                                std::cout << "Received from " << receivedID << ": " << messageContent << std::endl;
                                std::string command = formCommandFromMessage(messageContent);
                                std::string result = executeCommand(command);
                                std::string message = "User ID: " + std::to_string(clientID) + "\n" + result;
                                int bytesSent = sendto(listensock, message.c_str(), message.size(), 0,
                                               reinterpret_cast<struct sockaddr*>(&sAddr), sizeof(sAddr));
                                if (bytesSent < 0) {
                                    handleError("Error sending data");
                                }
                            }
                        }
                    } else {
                        handleError("Error receiving data");
                    }
                } 
            }
        }
    }

    close(listensock);
    return 0;
}