#pragma once
#include <cstdint>
#include <string>

#include "interfaces.hpp"
#include "net.hpp"

class TcpServer final : public IServer
{
public:
    explicit TcpServer(std::uint16_t port);
    void start() override;

private:
    std::uint16_t port_{};
    TcpSocket listenSock_;

    void handleClient(TcpSocket& client, const std::string& peer);
};