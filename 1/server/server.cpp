#include "server.hpp"
#include <iostream>
#include <unistd.h>


UdpServer::UdpServer(int port){
    _sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (_sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    _server_addr.sin_family = AF_INET;
    _server_addr.sin_addr.s_addr = INADDR_ANY;
    _server_addr.sin_port = htons(port);
    if (bind(_sockfd, (sockaddr*)&_server_addr, sizeof(_server_addr)) < 0) {
        perror("bind failed");
        close(_sockfd);
        exit(EXIT_FAILURE);
    }
}

void UdpServer::run(){
    char buffer[1024];
    sockaddr_in client{};
    socklen_t len = sizeof(client);

    while (true) {
        ssize_t n = recvfrom(_sockfd, buffer, 1023, 0,
                        (sockaddr*)&client, &len);
        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }
        buffer[n] = '\0';

        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client.sin_addr), ipstr, INET_ADDRSTRLEN);
        std::cout << "[" << ipstr << "] " << buffer << std::endl;
    }
}

UdpServer::~UdpServer(){
    close(_sockfd);
}