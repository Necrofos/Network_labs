#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

static int               g_sock = -1;
static std::atomic<bool> g_running{true};
static char              g_nickname[MAX_NAME] = "";
static std::atomic<uint32_t> g_msg_id{1};

// ─── Helpers ──────────────────────────────────────────────────────────────────

static std::string format_time(time_t ts) {
    char buf[32];
    struct tm* t = localtime(&ts);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    return buf;
}

static MessageEx make_msg(uint8_t type, const char* receiver, const char* payload) {
    MessageEx m{};
    m.type      = type;
    m.msg_id    = g_msg_id++;
    m.timestamp = time(nullptr);
    strncpy(m.sender,   g_nickname, MAX_NAME - 1);
    strncpy(m.receiver, receiver ? receiver : "", MAX_NAME - 1);
    strncpy(m.payload,  payload  ? payload  : "", MAX_PAYLOAD - 1);
    m.length = (uint32_t)strlen(m.payload);
    return m;
}

static ssize_t send_ex(const MessageEx& m) {
    return send(g_sock, &m, sizeof(MessageEx), 0);
}

// ─── Receive thread ───────────────────────────────────────────────────────────

static void receive_handler() {
    MessageEx msg{};
    while (g_running) {
        ssize_t n = recv(g_sock, &msg, sizeof(MessageEx), 0);
        if (n <= 0) {
            if (g_running)
                std::cout << "\n[Disconnected from server]" << std::endl;
            g_running = false;
            break;
        }

        switch (msg.type) {

        case MSG_TEXT:
        case MSG_PRIVATE:
            // Server already formats the display string in payload
            std::cout << "\n" << msg.payload << "\n> " << std::flush;
            break;

        case MSG_SERVER_INFO:
            std::cout << "\n[SERVER]: " << msg.payload << "\n> " << std::flush;
            break;

        case MSG_ERROR:
            std::cout << "\n[ERROR]: " << msg.payload << "\n> " << std::flush;
            break;

        case MSG_PONG:
            std::cout << "\n[SERVER]: PONG" << std::endl;
            std::cout << "[LOG]: i love cast (no segmentation faults pls)" << "\n> " << std::flush;
            break;

        case MSG_WELCOME:
            std::cout << "[Server]: " << msg.payload << std::endl;
            break;

        case MSG_HISTORY_DATA:
            std::cout << "\n" << msg.payload << std::endl;
            std::cout << "[LOG]: i love TCP/IP (don't tell UDP)" << "\n> " << std::flush;
            break;

        default:
            break;
        }
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
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
    MessageEx msg = make_msg(MSG_HELLO, "", "hello");
    send_ex(msg);

    ssize_t n = recv(g_sock, &msg, sizeof(MessageEx), 0);
    if (n <= 0 || msg.type != MSG_WELCOME) {
        std::cerr << "Handshake failed" << std::endl;
        close(g_sock);
        return 1;
    }
    std::cout << "[Server]: " << msg.payload << std::endl;

    // Nickname and authentication
    std::cout << "Enter nickname: ";
    std::cin >> g_nickname;
    std::cin.ignore();

    MessageEx auth = make_msg(MSG_AUTH, "", g_nickname);
    send_ex(auth);

    // Wait for auth response
    n = recv(g_sock, &msg, sizeof(MessageEx), 0);
    if (n <= 0) { close(g_sock); return 1; }

    if (msg.type == MSG_ERROR) {
        std::cout << "[ERROR]: " << msg.payload << std::endl;
        close(g_sock);
        return 1;
    }
    if (msg.type == MSG_SERVER_INFO)
        std::cout << "[SERVER]: " << msg.payload << std::endl;

    // Deliver any offline messages (server sends them before we enter the loop)
    // The receive thread will handle them once started.

    std::thread recv_thread(receive_handler);
    recv_thread.detach();

    // Input loop
    std::string input;
    while (g_running) {
        std::cout << "> ";
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        if (input == "/quit") {
            MessageEx bye = make_msg(MSG_BYE, "", "");
            send_ex(bye);
            g_running = false;
            break;

        } else if (input == "/ping") {
            MessageEx ping = make_msg(MSG_PING, "", "");
            send_ex(ping);

        } else if (input == "/list") {
            MessageEx lst = make_msg(MSG_LIST, "", "");
            send_ex(lst);

        } else if (input == "/history") {
            MessageEx hist = make_msg(MSG_HISTORY, "", "");
            send_ex(hist);

        } else if (input.rfind("/history ", 0) == 0) {
            std::string n_str = input.substr(9);
            if (n_str.empty() || n_str.find_first_not_of("0123456789") != std::string::npos) {
                std::cout << "Usage: /history [N]" << std::endl;
            } else {
                MessageEx hist = make_msg(MSG_HISTORY, "", n_str.c_str());
                send_ex(hist);
            }

        } else if (input == "/help") {
            std::cout << "Available commands:\n"
                         "/help\n"
                         "/list\n"
                         "/history\n"
                         "/history N\n"
                         "/quit\n"
                         "/w <nick> <message>\n"
                         "/ping\n"
                         "Tip: meow, message delivered" << std::endl;

        } else if (input.rfind("/w ", 0) == 0) {
            std::string rest = input.substr(3);
            auto sp = rest.find(' ');
            if (sp == std::string::npos) {
                std::cout << "Usage: /w <nick> <message>" << std::endl;
            } else {
                std::string target = rest.substr(0, sp);
                std::string text   = rest.substr(sp + 1);

                // Display locally with timestamp
                std::cout << "[" << format_time(time(nullptr)) << "]["
                          << g_nickname << " -> " << target << "]: "
                          << text << std::endl;
                std::cout << "[CLIENT]: hmm... TCP feels stable today" << std::endl;

                MessageEx pm = make_msg(MSG_PRIVATE, target.c_str(), text.c_str());
                send_ex(pm);
            }

        } else {
            // Plain text broadcast
            std::cout << "[" << format_time(time(nullptr)) << "][id="
                      << g_msg_id.load() << "][" << g_nickname << "]: "
                      << input << std::endl;
            std::cout << "[CLIENT]: hmm... TCP feels stable today" << std::endl;

            MessageEx tm = make_msg(MSG_TEXT, "", input.c_str());
            send_ex(tm);
        }
    }

    close(g_sock);
    return 0;
}
