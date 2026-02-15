#pragma once

#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>


#include <atomic>
#include <string>

class UdpClient{
public:
    UdpClient(const char* ip, int port);
    void sendMessage(const std::string& msg);
    void run();
    static std::string get_local_ip();
    static void handle_sigint(int);
    ~UdpClient();
    static std::atomic<bool> running;
private:
    int _sockfd;
    sockaddr_in _server{};
};