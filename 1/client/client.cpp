
#include "client.hpp"
#include <iostream>
#include <csignal>
#include <netdb.h>
std::atomic<bool> UdpClient::running(true);

void UdpClient::handle_sigint(int) {
    running = false;
}

std::string UdpClient::get_local_ip() {
    char hostbuffer[256];
    char *IPbuffer;
    struct hostent *host_entry;
    gethostname(hostbuffer, sizeof(hostbuffer));
    host_entry = gethostbyname(hostbuffer);
    if (host_entry && host_entry->h_addr_list[0]) {
        IPbuffer = inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0]));
        return std::string(IPbuffer);
    }
    return "Unknown";
}

void UdpClient::run() {
    signal(SIGINT, handle_sigint);
    std::cout << "Client started.IP: " << get_local_ip() << std::endl;
    std::string msg;
    while (running) {
        std::cout << "Enter message: ";
        if (!std::getline(std::cin, msg)) break;
        if (!running) break;
        if (!msg.empty())
            sendMessage(msg);
    }
    std::cout << "\nExiting client..." << std::endl;
}

UdpClient::UdpClient(const char* ip, int port){
    _sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (_sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    _server.sin_family = AF_INET;
    _server.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &_server.sin_addr) <= 0) {
        perror("inet_pton failed");
        close(_sockfd);
        exit(EXIT_FAILURE);
    }
}


void UdpClient::sendMessage(const std::string& msg) {
    ssize_t sent = sendto(_sockfd, msg.c_str(), msg.size(), 0,
            (sockaddr*)&_server, sizeof(_server));
    if (sent < 0) {
        perror("sendto failed");
        return;
    }
}


UdpClient::~UdpClient(){close(_sockfd);}