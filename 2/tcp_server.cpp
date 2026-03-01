#include "tcp_server.hpp"
#include <iostream>

TcpServer::TcpServer(std::uint16_t port)
    : port_(port), listenSock_(TcpSocket::create())
{
    listenSock_.setReuseAddr();
    listenSock_.bindAny(port_);
    listenSock_.listen(1);
}

void TcpServer::handleClient(TcpSocket& client, const std::string& peer)
{
    Message hello;
    if (!client.recvMessage(hello)) return;
    if (hello.type != MSG_HELLO) throw std::runtime_error("Protocol error");

    std::cout << "[" << peer << "]: " << hello.payload << "\n";

    client.sendMessage(Message{MSG_WELCOME, "Welcome " + peer});

    while (true)
    {
        Message in;
        if (!client.recvMessage(in)) break;

        if (in.type == MSG_TEXT)
        {
            std::cout << "[" << peer << "]: " << in.payload << "\n";
        }
        else if (in.type == MSG_PING)
        {
            client.sendMessage(Message{MSG_PONG, ""});
        }
        else if (in.type == MSG_BYE)
        {
            break;
        }
    }
}

void TcpServer::start()
{
    std::cout << "Listening on port " << port_ << "...\n";

    sockaddr_in caddr{};
    TcpSocket client = listenSock_.accept(&caddr);
    std::string peer = addrToString(caddr);

    std::cout << "Client connected\n";
    try
    {
        handleClient(client, peer);
    }
    catch (...)
    {
    }
    std::cout << "Client disconnected\n";
}