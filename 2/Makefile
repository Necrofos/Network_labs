CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

SRC_COMMON := net.cpp
SERVER_SRC := main_server.cpp tcp_server.cpp $(SRC_COMMON)
CLIENT_SRC := main_client.cpp tcp_client.cpp $(SRC_COMMON)

.PHONY: all clean

all: server client

server: $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC)

client: $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC)

clean:
	rm -f server client

