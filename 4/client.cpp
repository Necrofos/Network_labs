#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "protocol.h"

static int            g_sock      = -1;
static std::atomic<bool> g_running{true};
static char           g_nickname[MAX_NICK] = "";

// ─── Helpers ──────────────────────────────────────────────────────────────────

static ssize_t send_msg(Message& msg) {
    msg.length = (uint32_t)(1 + strlen(msg.payload));
    return send(g_sock, &msg, sizeof(uint32_t) + msg.length, 0);
}

static void send_text(const char* text) {
    Message msg;
    msg.type = MSG_TEXT;
    strncpy(msg.payload, text, MAX_PAYLOAD - 1);
    msg.payload[MAX_PAYLOAD - 1] = '\0';
    send_msg(msg);
}

static void send_private(const char* target, const char* text) {
    Message msg;
    msg.type = MSG_PRIVATE;
    snprintf(msg.payload, MAX_PAYLOAD, "%s:%s", target, text);
    send_msg(msg);
}

static void send_ping() {
    Message msg;
    msg.type = MSG_PING;
    msg.payload[0] = '\0';
    send_msg(msg);
}

static void send_bye() {
    Message msg;
    msg.type = MSG_BYE;
    msg.payload[0] = '\0';
    send_msg(msg);
}

// ─── Receive thread ───────────────────────────────────────────────────────────

static void receive_handler() {
    Message msg;
    while (g_running) {
        ssize_t n = recv(g_sock, &msg, sizeof(Message), 0);
        if (n <= 0) {
            if (g_running)
                std::cout << "\n[Disconnected from server]" << std::endl;
            g_running = false;
            break;
        }

        switch (msg.type) {
        case MSG_TEXT:
            std::cout << "\n" << msg.payload << "\n> " << std::flush;
            break;
        case MSG_PRIVATE:
            std::cout << "\n" << msg.payload << "\n> " << std::flush;
            break;
        case MSG_SERVER_INFO:
            std::cout << "\n[SERVER]: " << msg.payload << "\n> " << std::flush;
            break;
        case MSG_ERROR:
            std::cout << "\n[ERROR]: " << msg.payload << "\n> " << std::flush;
            break;
        case MSG_PONG:
            std::cout << "\n[SERVER]: PONG\n> " << std::flush;
            break;
        case MSG_WELCOME:
            std::cout << "[Server]: " << msg.payload << std::endl;
            break;
        default:
            break;
        }
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    // Connect
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(g_sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        std::cerr << "Connection failed" << std::endl;
        return 1;
    }
    std::cout << "Connected" << std::endl;

    // HELLO/WELCOME exchange
    Message msg;
    msg.type = MSG_HELLO;
    strncpy(msg.payload, "client", MAX_PAYLOAD - 1);
    msg.length = (uint32_t)(1 + strlen(msg.payload));
    send(g_sock, &msg, sizeof(uint32_t) + msg.length, 0);

    ssize_t n = recv(g_sock, &msg, sizeof(Message), 0);
    if (n <= 0 || msg.type != MSG_WELCOME) {
        std::cerr << "Handshake failed" << std::endl;
        close(g_sock);
        return 1;
    }
    std::cout << "[Server]: " << msg.payload << std::endl;

    // Ask for nickname and authenticate
    std::cout << "Enter nickname: ";
    std::cin >> g_nickname;
    std::cin.ignore();

    msg.type = MSG_AUTH;
    strncpy(msg.payload, g_nickname, MAX_PAYLOAD - 1);
    msg.payload[MAX_PAYLOAD - 1] = '\0';
    send_msg(msg);

    // Wait for auth response (SERVER_INFO or ERROR)
    n = recv(g_sock, &msg, sizeof(Message), 0);
    if (n <= 0) { close(g_sock); return 1; }

    if (msg.type == MSG_ERROR) {
        std::cout << "[ERROR]: " << msg.payload << std::endl;
        close(g_sock);
        return 1;
    }
    if (msg.type == MSG_SERVER_INFO)
        std::cout << "[SERVER]: " << msg.payload << std::endl;

    // Start receive thread
    std::thread recv_thread(receive_handler);
    recv_thread.detach();

    // Input loop
    std::string input;
    while (g_running) {
        std::cout << "> ";
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        if (input == "/quit") {
            send_bye();
            g_running = false;
            break;
        } else if (input == "/ping") {
            send_ping();
        } else if (input.rfind("/w ", 0) == 0) {
            // /w <nick> <message>
            std::string rest = input.substr(3);
            auto sp = rest.find(' ');
            if (sp == std::string::npos) {
                std::cout << "Usage: /w <nick> <message>" << std::endl;
            } else {
                std::string target = rest.substr(0, sp);
                std::string text   = rest.substr(sp + 1);
                send_private(target.c_str(), text.c_str());
            }
        } else {
            send_text(input.c_str());
        }
    }

    close(g_sock);
    return 0;
}
