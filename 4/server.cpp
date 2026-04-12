#include <iostream>
#include <vector>
#include <queue>
#include <string>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "protocol.h"

// ─── OSI layer logging ────────────────────────────────────────────────────────

static void osi_recv_log(const char* app_msg) {
    std::cout << "[Layer 4 - Transport]     recv()" << std::endl;
    std::cout << "[Layer 6 - Presentation]  deserialize Message" << std::endl;
    std::cout << "[Layer 5 - Session]       " << app_msg << std::endl;
}

static void osi_handle_log(const char* msg_name) {
    std::cout << "[Layer 7 - Application]   handle " << msg_name << std::endl;
}

static void osi_send_log(const char* app_msg) {
    std::cout << "[Layer 7 - Application]   " << app_msg << std::endl;
    std::cout << "[Layer 6 - Presentation]  serialize Message" << std::endl;
    std::cout << "[Layer 4 - Transport]     send()" << std::endl;
}

// ─── Client registry ─────────────────────────────────────────────────────────

struct Client {
    int  sock;
    char nickname[MAX_NICK];
    int  authenticated;
};

static std::vector<Client> g_clients;
static pthread_mutex_t     g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// ─── Connection queue (thread pool) ──────────────────────────────────────────

static std::queue<int>    g_queue;
static pthread_mutex_t    g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t     g_queue_cond  = PTHREAD_COND_INITIALIZER;

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Must be called with g_clients_mutex held.
static bool nick_taken(const char* nick) {
    for (const auto& c : g_clients)
        if (c.authenticated && strcmp(c.nickname, nick) == 0)
            return true;
    return false;
}

static ssize_t send_msg(int fd, Message& msg) {
    msg.length = (uint32_t)(1 + strlen(msg.payload));
    return send(fd, &msg, sizeof(uint32_t) + msg.length, 0);
}

static void send_error(int fd, const char* text) {
    Message msg;
    msg.type = MSG_ERROR;
    strncpy(msg.payload, text, MAX_PAYLOAD - 1);
    msg.payload[MAX_PAYLOAD - 1] = '\0';
    osi_send_log("prepare MSG_ERROR");
    send_msg(fd, msg);
}

static void send_server_info(int fd, const char* text) {
    Message msg;
    msg.type = MSG_SERVER_INFO;
    strncpy(msg.payload, text, MAX_PAYLOAD - 1);
    msg.payload[MAX_PAYLOAD - 1] = '\0';
    osi_send_log("prepare MSG_SERVER_INFO");
    send_msg(fd, msg);
}

static void broadcast_text(const char* sender_nick, const char* text) {
    Message msg;
    msg.type = MSG_TEXT;
    snprintf(msg.payload, MAX_PAYLOAD, "[%s]: %s", sender_nick, text);

    osi_send_log(("broadcast MSG_TEXT from " + std::string(sender_nick)).c_str());

    pthread_mutex_lock(&g_clients_mutex);
    for (const auto& c : g_clients) {
        if (c.authenticated)
            send_msg(c.sock, msg);
    }
    pthread_mutex_unlock(&g_clients_mutex);
}

static void send_private(int fd, const char* sender, const char* text) {
    Message msg;
    msg.type = MSG_PRIVATE;
    snprintf(msg.payload, MAX_PAYLOAD, "[PRIVATE][%s]: %s", sender, text);
    osi_send_log("prepare MSG_PRIVATE");
    send_msg(fd, msg);
}

// ─── Worker thread ────────────────────────────────────────────────────────────

static void remove_client(int fd) {
    pthread_mutex_lock(&g_clients_mutex);
    for (auto it = g_clients.begin(); it != g_clients.end(); ++it) {
        if (it->sock == fd) { g_clients.erase(it); break; }
    }
    pthread_mutex_unlock(&g_clients_mutex);
}

void* worker_thread(void*) {
    while (true) {
        // Pop next fd from queue
        pthread_mutex_lock(&g_queue_mutex);
        while (g_queue.empty())
            pthread_cond_wait(&g_queue_cond, &g_queue_mutex);
        int fd = g_queue.front();
        g_queue.pop();
        pthread_mutex_unlock(&g_queue_mutex);

        std::cout << "\nClient connected (fd=" << fd << ")" << std::endl;

        Message msg;

        // ── Initial HELLO/WELCOME exchange (preserved from Lab 3) ──────────
        ssize_t n = recv(fd, &msg, sizeof(Message), 0);
        if (n <= 0 || msg.type != MSG_HELLO) {
            close(fd);
            continue;
        }
        msg.type = MSG_WELCOME;
        strncpy(msg.payload, "Welcome to the server!", MAX_PAYLOAD - 1);
        osi_send_log("prepare MSG_WELCOME");
        send_msg(fd, msg);

        // ── Authentication phase ────────────────────────────────────────────
        char nickname[MAX_NICK] = "";
        bool authed = false;

        while (!authed) {
            n = recv(fd, &msg, sizeof(Message), 0);
            if (n <= 0) goto disconnect;

            osi_recv_log("checking authentication");

            if (msg.type != MSG_AUTH) {
                osi_handle_log("(ignored, not authenticated)");
                continue;
            }

            // Extract nickname from payload
            strncpy(nickname, msg.payload, MAX_NICK - 1);
            nickname[MAX_NICK - 1] = '\0';

            if (nickname[0] == '\0') {
                send_error(fd, "Nickname cannot be empty");
                close(fd);
                goto next_client;
            }

            pthread_mutex_lock(&g_clients_mutex);
            bool taken = nick_taken(nickname);
            pthread_mutex_unlock(&g_clients_mutex);

            if (taken) {
                send_error(fd, "Nickname already taken");
                close(fd);
                goto next_client;
            }

            // Register client
            {
                Client c;
                c.sock          = fd;
                c.authenticated = 1;
                strncpy(c.nickname, nickname, MAX_NICK - 1);
                c.nickname[MAX_NICK - 1] = '\0';

                pthread_mutex_lock(&g_clients_mutex);
                g_clients.push_back(c);
                pthread_mutex_unlock(&g_clients_mutex);
            }

            osi_recv_log("client authenticated");
            std::cout << "User [" << nickname << "] connected" << std::endl;

            send_server_info(fd, "Authentication successful. Welcome!");
            authed = true;
        }

        // ── Main message loop ───────────────────────────────────────────────
        while (true) {
            n = recv(fd, &msg, sizeof(Message), 0);
            if (n <= 0) break;

            osi_recv_log("client authenticated");

            switch (msg.type) {

            case MSG_TEXT: {
                osi_handle_log("MSG_TEXT");
                std::cout << "[" << nickname << "]: " << msg.payload << std::endl;
                broadcast_text(nickname, msg.payload);
                break;
            }

            case MSG_PRIVATE: {
                osi_handle_log("MSG_PRIVATE");
                // Payload format: "target_nick:message text"
                char target[MAX_NICK] = "";
                char text[MAX_PAYLOAD] = "";
                char* colon = strchr(msg.payload, ':');
                if (colon) {
                    int tlen = (int)(colon - msg.payload);
                    if (tlen >= MAX_NICK) tlen = MAX_NICK - 1;
                    strncpy(target, msg.payload, tlen);
                    target[tlen] = '\0';
                    strncpy(text, colon + 1, MAX_PAYLOAD - 1);
                    text[MAX_PAYLOAD - 1] = '\0';
                } else {
                    send_error(fd, "Invalid /w format");
                    break;
                }

                pthread_mutex_lock(&g_clients_mutex);
                int target_fd = -1;
                for (const auto& c : g_clients)
                    if (c.authenticated && strcmp(c.nickname, target) == 0)
                        { target_fd = c.sock; break; }
                pthread_mutex_unlock(&g_clients_mutex);

                if (target_fd < 0) {
                    send_error(fd, (std::string("User not found: ") + target).c_str());
                } else {
                    std::cout << "[PRIVATE][" << nickname << " -> " << target << "]: " << text << std::endl;
                    send_private(target_fd, nickname, text);
                }
                break;
            }

            case MSG_PING: {
                osi_handle_log("MSG_PING");
                msg.type = MSG_PONG;
                msg.payload[0] = '\0';
                osi_send_log("prepare MSG_PONG");
                send_msg(fd, msg);
                break;
            }

            case MSG_BYE: {
                osi_handle_log("MSG_BYE");
                goto disconnect;
            }

            default:
                break;
            }
        }

    disconnect:
        remove_client(fd);
        close(fd);
        std::cout << "User [" << nickname << "] disconnected" << std::endl;

    next_client:;
    }
    return nullptr;
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    pthread_t pool[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
        pthread_create(&pool[i], nullptr, worker_thread, nullptr);

    std::cout << "Server started on port " << PORT
              << " with " << THREAD_POOL_SIZE << " threads." << std::endl;

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) continue;
        pthread_mutex_lock(&g_queue_mutex);
        g_queue.push(client_fd);
        pthread_cond_signal(&g_queue_cond);
        pthread_mutex_unlock(&g_queue_mutex);
    }

    return 0;
}
