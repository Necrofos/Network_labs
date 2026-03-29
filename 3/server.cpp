#include <iostream>
#include <vector>
#include <queue>
#include <set>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "protocol.h"

std::queue<int> connection_queue;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

std::set<int> active_clients;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void broadcast(Message& msg, int sender_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int fd : active_clients) {
        send(fd, &msg, sizeof(uint32_t) + msg.length, 0);
    }
    pthread_mutex_unlock(&clients_mutex);
}

void* worker_thread(void* arg) {
    while (true) {
        int client_fd;

        pthread_mutex_lock(&queue_mutex);
        while (connection_queue.empty()) {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
        client_fd = connection_queue.front();
        connection_queue.pop();
        pthread_mutex_unlock(&queue_mutex);

        std::cout << "[Thread " << pthread_self() << "] Handling client FD: " << client_fd << std::endl;

        Message msg;
        char client_name[MAX_PAYLOAD] = "Unknown";

        if (recv(client_fd, &msg, sizeof(Message), 0) > 0 && msg.type == MSG_HELLO) {
            strncpy(client_name, msg.payload, MAX_PAYLOAD);

            msg.type = MSG_WELCOME;
            strcpy(msg.payload, "Welcome to the server!");
            msg.length = 1 + strlen(msg.payload);
            send(client_fd, &msg, sizeof(uint32_t) + msg.length, 0);

            pthread_mutex_lock(&clients_mutex);
            active_clients.insert(client_fd);
            pthread_mutex_unlock(&clients_mutex);
            
            std::cout << "User " << client_name << " connected." << std::endl;
        }

        while (true) {
            ssize_t bytes = recv(client_fd, &msg, sizeof(Message), 0);
            if (bytes <= 0 || msg.type == MSG_BYE) break;

            if (msg.type == MSG_TEXT) {
                std::cout << "[" << client_name << "]: " << msg.payload << std::endl;
                broadcast(msg, client_fd);
            } 
            else if (msg.type == MSG_PING) {
                msg.type = MSG_PONG;
                msg.length = 1;
                send(client_fd, &msg, sizeof(uint32_t) + msg.length, 0);
            }
        }

        close(client_fd);
        pthread_mutex_lock(&clients_mutex);
        active_clients.erase(client_fd);
        pthread_mutex_unlock(&clients_mutex);
        std::cout << "User " << client_name << " disconnected." << std::endl;
    }
    return nullptr;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr = {AF_INET, htons(PORT), INADDR_ANY};

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);

    pthread_t pool[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&pool[i], nullptr, worker_thread, nullptr);
    }

    std::cout << "Server started on port " << PORT << " with " << THREAD_POOL_SIZE << " threads." << std::endl;

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        pthread_mutex_lock(&queue_mutex);
        connection_queue.push(client_fd);
        pthread_cond_signal(&queue_cond);
        pthread_mutex_unlock(&queue_mutex);
    }

    return 0;
}