#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "protocol.h"

int sock = -1;
bool connected = false;

void receive_handler() {
    Message msg;
    while (true) {
        if (connected) {
            ssize_t bytes = recv(sock, &msg, sizeof(Message), 0);
            if (bytes <= 0) {
                std::cout << "\n[Disconnected from server]" << std::endl;
                connected = false;
                continue;
            }
            if (msg.type == MSG_TEXT) {
                std::cout << "\nBroadcast: " << msg.payload << "\n> " << std::flush;
            } else if (msg.type == MSG_PONG) {
                std::cout << "\n[Server PONG]\n> " << std::flush;
            } else if (msg.type == MSG_WELCOME) {
                std::cout << "\n[Server]: " << msg.payload << "\n> " << std::flush;
            }
        }
        usleep(100000);
    }
}

void connect_to_server(const char* name) {
    while (!connected) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr = {AF_INET, htons(PORT)};
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
            Message msg;
            msg.type = MSG_HELLO;
            strncpy(msg.payload, name, MAX_PAYLOAD);
            msg.length = 1 + strlen(msg.payload);
            send(sock, &msg, sizeof(uint32_t) + msg.length, 0);
            
            connected = true;
            std::cout << "Connected to server!" << std::endl;
        } else {
            std::cout << "Connection failed. Retrying in 2s..." << std::endl;
            close(sock);
            sleep(2);
        }
    }
}

int main() {
    char name[100];
    std::cout << "Enter your nickname: ";
    std::cin >> name;
    std::cin.ignore();

    std::thread(receive_handler).detach();

    while (true) {
        if (!connected) {
            connect_to_server(name);
        }

        std::cout << "> ";
        std::string input;
        std::getline(std::cin, input);

        if (input.empty()) continue;

        Message msg;
        if (input == "/quit") {
            msg.type = MSG_BYE;
            msg.length = 1;
            send(sock, &msg, sizeof(uint32_t) + msg.length, 0);
            break;
        } else if (input == "/ping") {
            msg.type = MSG_PING;
            msg.length = 1;
        } else {
            msg.type = MSG_TEXT;
            strncpy(msg.payload, input.c_str(), MAX_PAYLOAD);
            msg.length = 1 + input.length();
        }

        if (send(sock, &msg, sizeof(uint32_t) + msg.length, 0) <= 0) {
            connected = false;
        }
    }

    close(sock);
    return 0;
}