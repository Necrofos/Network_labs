#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "protocol.hpp"

class SysError : public std::runtime_error
{
public:
    explicit SysError(const std::string& what)
        : std::runtime_error(what + ": " + std::string(std::strerror(errno))) {}
};

class TcpSocket
{
    int fd_{-1};

public:
    TcpSocket() = default;
    explicit TcpSocket(int fd);

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& o) noexcept;
    TcpSocket& operator=(TcpSocket&& o) noexcept;

    ~TcpSocket();

    int fd() const;
    bool valid() const;

    void close();
    void shutdownBoth();

    static TcpSocket create();

    void setReuseAddr();
    void bindAny(std::uint16_t port);
    void listen(int backlog);
    TcpSocket accept(sockaddr_in* outAddr = nullptr);
    void connectTo(const std::string& hostIp, std::uint16_t port);

    void sendMessage(const Message& msg);
    bool recvMessage(Message& msg);

private:
    void sendAll(const void* data, std::size_t n);
    bool recvAll(void* data, std::size_t n);
};

std::string addrToString(const sockaddr_in& a);