CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

CLIENT_SRC = client/client.cpp client/main.cpp
CLIENT_HDR = client/client.hpp
CLIENT_BIN = client_app

SERVER_SRC = server/server.cpp server/main.cpp
SERVER_HDR = server/server.hpp
SERVER_BIN = server_app

all: $(CLIENT_BIN) $(SERVER_BIN)

$(CLIENT_BIN): $(CLIENT_SRC) $(CLIENT_HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(CLIENT_SRC)

$(SERVER_BIN): $(SERVER_SRC) $(SERVER_HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SERVER_SRC)

clean:
	rm -f $(CLIENT_BIN) $(SERVER_BIN)
