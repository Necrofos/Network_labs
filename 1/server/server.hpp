#pragma once

#include <arpa/inet.h>


class UdpServer{
public:
    explicit UdpServer(int port);

    void run();

    ~UdpServer();

private:
    int _sockfd;
    sockaddr_in _server_addr;
};