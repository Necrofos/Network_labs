#include <iostream>
#include "tcp_client.hpp"

int main(int argc, char** argv)
{
    std::string host = "127.0.0.1";
    int port = 5000;
    std::string nick = "Hello";

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);
    if (argc >= 4) nick = argv[3];

    TcpClient client(host, static_cast<std::uint16_t>(port), nick);
    client.start();
    return 0;
}