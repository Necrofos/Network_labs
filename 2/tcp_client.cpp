#include "tcp_client.hpp"
#include <iostream>
#include <stdexcept>
#include <sys/select.h>
#include <unistd.h>

TcpClient::TcpClient(std::string hostIp, std::uint16_t port, std::string nick)
    : host_(std::move(hostIp)), port_(port), nick_(std::move(nick))
{
}

void TcpClient::connectAndHandshake()
{
    sock_ = TcpSocket::create();
    sock_.connectTo(host_, port_);

    std::cout << "Connected\n";

    sock_.sendMessage(Message{MSG_HELLO, nick_});

    Message w;
    if (!sock_.recvMessage(w)) throw std::runtime_error("Disconnected");
    if (w.type != MSG_WELCOME) throw std::runtime_error("Protocol error");

    std::cout << w.payload << "\n";
}

bool TcpClient::handleStdinLine(const std::string& line)
{
    if (line == "/ping")
    {
        sock_.sendMessage(Message{MSG_PING, ""});
        return true;
    }
    if (line == "/quit")
    {
        sock_.sendMessage(Message{MSG_BYE, ""});
        return false;
    }

    sock_.sendMessage(Message{MSG_TEXT, line});
    return true;
}

bool TcpClient::handleIncoming()
{
    Message m;
    if (!sock_.recvMessage(m)) return false;

    if (m.type == MSG_TEXT)
    {
        std::cout << "\n" << m.payload << "\n";
    }
    else if (m.type == MSG_PONG)
    {
        std::cout << "\nPONG\n";
    }
    else if (m.type == MSG_BYE)
    {
        return false;
    }

    return true;
}

void TcpClient::eventLoop()
{
    int sfd = sock_.fd();
    int ifd = fileno(stdin);

    std::string line;
    std::cout << "> " << std::flush;

    while (true)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sfd, &rfds);
        FD_SET(ifd, &rfds);

        int maxfd = (sfd > ifd ? sfd : ifd);

        int r = ::select(maxfd + 1, &rfds, nullptr, nullptr, nullptr);
        if (r < 0)
        {
            if (errno == EINTR) continue;
            throw SysError("select");
        }

        if (FD_ISSET(sfd, &rfds))
        {
            if (!handleIncoming()) break;
            std::cout << "> " << std::flush;
        }

        if (FD_ISSET(ifd, &rfds))
        {
            if (!std::getline(std::cin, line)) break;

            bool cont = true;
            cont = handleStdinLine(line);

            if (!cont) break;
            std::cout << "> " << std::flush;
        }
    }
}

void TcpClient::start()
{
    try
    {
        connectAndHandshake();
        eventLoop();
    }
    catch (...)
    {
    }

    try { sock_.shutdownBoth(); } catch (...) {}
    sock_.close();

    std::cout << "Disconnected\n";
}