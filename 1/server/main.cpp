#include "server.hpp"

int main(){
    UdpServer server(8000);
    server.run();
    return 0;
}