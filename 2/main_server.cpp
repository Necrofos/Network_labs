#include <iostream>
#include "tcp_server.hpp"

int main(int argc, char** argv)
{
    int port = 5000;
    if (argc >= 2) port = std::stoi(argv[1]);

    TcpServer server(static_cast<std::uint16_t>(port));
    server.start();
    return 0;
}