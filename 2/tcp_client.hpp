#pragma once
#include <cstdint>
#include <string>

#include "interfaces.hpp"
#include "net.hpp"

class TcpClient final : public IClient
{
public:
    TcpClient(std::string hostIp, std::uint16_t port, std::string nick);
    void start() override;

private:
    std::string host_;
    std::uint16_t port_{};
    std::string nick_;
    TcpSocket sock_;

    void connectAndHandshake();
    void eventLoop();

    bool handleStdinLine(const std::string& line);
    bool handleIncoming();
};