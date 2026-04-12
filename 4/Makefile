CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread

all: server client

server: server.cpp protocol.h
	$(CXX) $(CXXFLAGS) -o server server.cpp

client: client.cpp protocol.h
	$(CXX) $(CXXFLAGS) -o client client.cpp

clean:
	rm -f server client

.PHONY: all clean
