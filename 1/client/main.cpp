#include "client.hpp"

int main(){
    UdpClient client("127.0.0.1", 8000);
    client.run();
    return 0;
}