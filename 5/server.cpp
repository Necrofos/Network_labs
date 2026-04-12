#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <queue>
#include <map>
#include <string>
#include <atomic>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include "protocol.h"

// ─── Global message ID counter ────────────────────────────────────────────────

static std::atomic<uint32_t> g_msg_id{1};

// ─── Client registry ─────────────────────────────────────────────────────────

struct Client {
    int    sock;
    char   nickname[MAX_NAME];
    char   ip[INET_ADDRSTRLEN];
    int    port;
};

static std::vector<Client> g_clients;
static pthread_mutex_t     g_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// ─── Offline queue ────────────────────────────────────────────────────────────

struct OfflineMsg {
    char     sender[MAX_NAME];
    char     receiver[MAX_NAME];
    char     text[MAX_PAYLOAD];
    time_t   timestamp;
    uint32_t msg_id;
};

static std::map<std::string, std::vector<OfflineMsg>> g_offline;
static pthread_mutex_t g_offline_mutex = PTHREAD_MUTEX_INITIALIZER;

// ─── History ──────────────────────────────────────────────────────────────────

#define HISTORY_FILE "chat_history.jsonl"

struct HistoryRecord {
    uint32_t    msg_id;
    time_t      timestamp;
    char        sender[MAX_NAME];
    char        receiver[MAX_NAME];
    uint8_t     type;
    char        text[MAX_PAYLOAD];
    bool        delivered;
    bool        is_offline;
};

static std::vector<HistoryRecord> g_history;
static pthread_mutex_t            g_history_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char* type_name(uint8_t t) {
    switch (t) {
    case MSG_TEXT:    return "MSG_TEXT";
    case MSG_PRIVATE: return "MSG_PRIVATE";
    default:          return "MSG_OTHER";
    }
}

static std::string format_time(time_t ts) {
    char buf[32];
    struct tm* tm_info = localtime(&ts);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    return buf;
}

static void append_history(const HistoryRecord& rec) {
    // Write one JSON object per line (JSONL)
    std::ofstream f(HISTORY_FILE, std::ios::app);
    if (!f) return;
    f << "{"
      << "\"msg_id\":" << rec.msg_id << ","
      << "\"timestamp\":" << (long long)rec.timestamp << ","
      << "\"sender\":\"" << rec.sender << "\","
      << "\"receiver\":\"" << rec.receiver << "\","
      << "\"type\":\"" << type_name(rec.type) << "\","
      << "\"text\":\"" << rec.text << "\","
      << "\"delivered\":" << (rec.delivered ? "true" : "false") << ","
      << "\"is_offline\":" << (rec.is_offline ? "true" : "false")
      << "}\n";
}

static void save_history(const HistoryRecord& rec) {
    pthread_mutex_lock(&g_history_mutex);
    g_history.push_back(rec);
    append_history(rec);
    pthread_mutex_unlock(&g_history_mutex);
}

// ─── Thread pool queue ────────────────────────────────────────────────────────

static std::queue<int>  g_queue;
static pthread_mutex_t  g_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_queue_cond  = PTHREAD_COND_INITIALIZER;

// ─── TCP/IP layer logging ─────────────────────────────────────────────────────

static void log_recv(const char* src_ip, int src_port, size_t bytes, const char* msg_type) {
    std::cout << "[Network Access] frame received via network interface" << std::endl;
    std::cout << "[Internet]       src=" << src_ip
              << " dst=127.0.0.1 proto=TCP" << std::endl;
    std::cout << "[Transport]      recv() " << bytes << " bytes via TCP"
              << "  src_port=" << src_port << " dst_port=" << PORT << std::endl;
    std::cout << "[Application]    deserialize MessageEx -> " << msg_type << std::endl;
}

static void log_send(const char* dst_ip, const char* msg_type) {
    std::cout << "[Application]    prepare " << msg_type << std::endl;
    std::cout << "[Transport]      send() via TCP" << std::endl;
    std::cout << "[Internet]       destination ip = " << dst_ip << std::endl;
    std::cout << "[Network Access] frame sent to network interface" << std::endl;
}

// ─── Message helpers ──────────────────────────────────────────────────────────

static MessageEx make_msg(uint8_t type, const char* sender, const char* receiver,
                          const char* payload) {
    MessageEx m{};
    m.type      = type;
    m.msg_id    = g_msg_id++;
    m.timestamp = time(nullptr);
    strncpy(m.sender,   sender   ? sender   : "",  MAX_NAME - 1);
    strncpy(m.receiver, receiver ? receiver : "",  MAX_NAME - 1);
    strncpy(m.payload,  payload  ? payload  : "",  MAX_PAYLOAD - 1);
    m.length    = (uint32_t)strlen(m.payload);
    return m;
}

static ssize_t send_ex(int fd, const MessageEx& m) {
    return send(fd, &m, sizeof(MessageEx), 0);
}

static void send_error(int fd, const char* dst_ip, const char* text) {
    MessageEx m = make_msg(MSG_ERROR, "SERVER", "", text);
    log_send(dst_ip, "MSG_ERROR");
    send_ex(fd, m);
}

static void send_server_info(int fd, const char* dst_ip, const char* text) {
    MessageEx m = make_msg(MSG_SERVER_INFO, "SERVER", "", text);
    log_send(dst_ip, "MSG_SERVER_INFO");
    send_ex(fd, m);
}

// ─── Broadcast & private delivery ────────────────────────────────────────────

static void broadcast(const MessageEx& msg) {
    pthread_mutex_lock(&g_clients_mutex);
    for (const auto& c : g_clients) {
        log_send(c.ip, type_name(msg.type));
        send_ex(c.sock, msg);
    }
    pthread_mutex_unlock(&g_clients_mutex);
}

// Must be called WITHOUT g_clients_mutex held.
static bool deliver_private(const MessageEx& msg) {
    pthread_mutex_lock(&g_clients_mutex);
    int target_fd = -1;
    char target_ip[INET_ADDRSTRLEN] = "127.0.0.1";
    for (const auto& c : g_clients)
        if (strcmp(c.nickname, msg.receiver) == 0)
            { target_fd = c.sock; strncpy(target_ip, c.ip, sizeof(target_ip)-1); break; }
    pthread_mutex_unlock(&g_clients_mutex);

    if (target_fd < 0) return false;
    log_send(target_ip, "MSG_PRIVATE");
    send_ex(target_fd, msg);
    return true;
}

// ─── Worker thread ────────────────────────────────────────────────────────────

static void remove_client(int fd) {
    pthread_mutex_lock(&g_clients_mutex);
    for (auto it = g_clients.begin(); it != g_clients.end(); ++it)
        if (it->sock == fd) { g_clients.erase(it); break; }
    pthread_mutex_unlock(&g_clients_mutex);
}

void* worker_thread(void*) {
    while (true) {
        pthread_mutex_lock(&g_queue_mutex);
        while (g_queue.empty())
            pthread_cond_wait(&g_queue_cond, &g_queue_mutex);
        int fd = g_queue.front();
        g_queue.pop();
        pthread_mutex_unlock(&g_queue_mutex);

        // Get client address
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        getpeername(fd, (sockaddr*)&peer, &peer_len);
        char src_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, src_ip, sizeof(src_ip));
        int  src_port = ntohs(peer.sin_port);

        std::cout << "\nClient connected from " << src_ip << ":" << src_port << std::endl;

        MessageEx msg{};

        // ── Initial HELLO/WELCOME (preserved from Lab 3) ────────────────────
        ssize_t n = recv(fd, &msg, sizeof(MessageEx), 0);
        if (n <= 0 || msg.type != MSG_HELLO) { close(fd); continue; }

        MessageEx welcome = make_msg(MSG_WELCOME, "SERVER", "", "Welcome to the server!");
        log_send(src_ip, "MSG_WELCOME");
        send_ex(fd, welcome);

        // ── Authentication ───────────────────────────────────────────────────
        char nickname[MAX_NAME] = "";
        bool authed = false;

        while (!authed) {
            n = recv(fd, &msg, sizeof(MessageEx), 0);
            if (n <= 0) goto disconnect;

            log_recv(src_ip, src_port, (size_t)n, type_name(msg.type));

            if (msg.type != MSG_AUTH) continue;

            strncpy(nickname, msg.payload, MAX_NAME - 1);
            nickname[MAX_NAME - 1] = '\0';

            if (nickname[0] == '\0') {
                send_error(fd, src_ip, "Nickname cannot be empty");
                close(fd); goto next_client;
            }

            pthread_mutex_lock(&g_clients_mutex);
            bool taken = false;
            for (const auto& c : g_clients)
                if (strcmp(c.nickname, nickname) == 0) { taken = true; break; }
            pthread_mutex_unlock(&g_clients_mutex);

            if (taken) {
                send_error(fd, src_ip, "Nickname already taken");
                close(fd); goto next_client;
            }

            {
                Client c;
                c.sock = fd;
                c.port = src_port;
                strncpy(c.nickname, nickname, MAX_NAME - 1);
                c.nickname[MAX_NAME - 1] = '\0';
                strncpy(c.ip, src_ip, sizeof(c.ip) - 1);

                pthread_mutex_lock(&g_clients_mutex);
                g_clients.push_back(c);
                pthread_mutex_unlock(&g_clients_mutex);
            }

            std::cout << "[Application]    authentication success: " << nickname << std::endl;
            std::cout << "User [" << nickname << "] connected" << std::endl;

            send_server_info(fd, src_ip, "Authentication successful. Welcome!");
            authed = true;
        }

        // ── Deliver offline messages ─────────────────────────────────────────
        {
            pthread_mutex_lock(&g_offline_mutex);
            auto it = g_offline.find(nickname);
            if (it == g_offline.end() || it->second.empty()) {
                std::cout << "[Application]    no offline messages for " << nickname << std::endl;
            } else {
                for (const auto& om : it->second) {
                    char text[512];
                    snprintf(text, sizeof(text), "[OFFLINE][%s -> %s]: %s",
                             om.sender, om.receiver, om.text);
                    text[sizeof(text) - 1] = '\0';

                    MessageEx dm = make_msg(MSG_PRIVATE, om.sender, om.receiver,
                                            text);
                    dm.timestamp = om.timestamp;
                    dm.msg_id    = om.msg_id;

                    log_send(src_ip, "MSG_PRIVATE (offline delivery)");
                    send_ex(fd, dm);

                    std::cout << "[Application]    delivered offline msg id=" << om.msg_id
                              << " to " << nickname << std::endl;
                }
                it->second.clear();
            }
            pthread_mutex_unlock(&g_offline_mutex);
        }

        // ── Main message loop ────────────────────────────────────────────────
        while (true) {
            n = recv(fd, &msg, sizeof(MessageEx), 0);
            if (n <= 0) break;

            log_recv(src_ip, src_port, (size_t)n, type_name(msg.type));

            switch (msg.type) {

            case MSG_TEXT: {
                std::cout << "[Application]    handle MSG_TEXT" << std::endl;

                char display[MAX_PAYLOAD + 128];
                snprintf(display, sizeof(display), "[%s][id=%u][%s]: %s",
                         format_time(msg.timestamp).c_str(),
                         msg.msg_id, msg.sender, msg.payload);

                std::cout << display << std::endl;

                MessageEx bcast = make_msg(MSG_TEXT, msg.sender, "", display);
                broadcast(bcast);

                HistoryRecord hr{};
                hr.msg_id    = msg.msg_id;
                hr.timestamp = msg.timestamp;
                strncpy(hr.sender,   msg.sender,  MAX_NAME - 1);
                hr.receiver[0] = '\0';
                hr.type      = MSG_TEXT;
                strncpy(hr.text, msg.payload, MAX_PAYLOAD - 1);
                hr.delivered = true;
                hr.is_offline = false;
                save_history(hr);
                break;
            }

            case MSG_PRIVATE: {
                std::cout << "[Application]    handle MSG_PRIVATE" << std::endl;

                char text[MAX_PAYLOAD];
                strncpy(text, msg.payload, MAX_PAYLOAD - 1);
                text[MAX_PAYLOAD - 1] = '\0';

                // Check if receiver is online
                pthread_mutex_lock(&g_clients_mutex);
                bool online = false;
                for (const auto& c : g_clients)
                    if (strcmp(c.nickname, msg.receiver) == 0) { online = true; break; }
                pthread_mutex_unlock(&g_clients_mutex);

                HistoryRecord hr{};
                hr.msg_id    = msg.msg_id;
                hr.timestamp = msg.timestamp;
                strncpy(hr.sender,   msg.sender,   MAX_NAME - 1);
                strncpy(hr.receiver, msg.receiver,  MAX_NAME - 1);
                hr.type      = MSG_PRIVATE;
                strncpy(hr.text, text, MAX_PAYLOAD - 1);

                if (online) {
                    char display[MAX_PAYLOAD + 128];
                    snprintf(display, sizeof(display),
                             "[%s][id=%u][PRIVATE][%s -> %s]: %s",
                             format_time(msg.timestamp).c_str(),
                             msg.msg_id, msg.sender, msg.receiver, text);

                    std::cout << display << std::endl;

                    MessageEx pm = make_msg(MSG_PRIVATE, msg.sender, msg.receiver, display);
                    deliver_private(pm);

                    hr.delivered  = true;
                    hr.is_offline = false;
                } else {
                    std::cout << "[Application]    receiver " << msg.receiver
                              << " is offline" << std::endl;
                    std::cout << "[Application]    store message in offline queue" << std::endl;

                    OfflineMsg om{};
                    strncpy(om.sender,   msg.sender,   MAX_NAME - 1);
                    strncpy(om.receiver, msg.receiver,  MAX_NAME - 1);
                    strncpy(om.text,     text,          MAX_PAYLOAD - 1);
                    om.timestamp = msg.timestamp;
                    om.msg_id    = msg.msg_id;

                    pthread_mutex_lock(&g_offline_mutex);
                    g_offline[msg.receiver].push_back(om);
                    pthread_mutex_unlock(&g_offline_mutex);

                    std::cout << "[Application]    append record to history file delivered=false"
                              << std::endl;

                    hr.delivered  = false;
                    hr.is_offline = true;
                }
                save_history(hr);
                break;
            }

            case MSG_PING: {
                std::cout << "[Application]    handle MSG_PING" << std::endl;
                MessageEx pong = make_msg(MSG_PONG, "SERVER", msg.sender, "PONG");
                log_send(src_ip, "MSG_PONG");
                send_ex(fd, pong);
                break;
            }

            case MSG_LIST: {
                std::cout << "[Application]    handle MSG_LIST" << std::endl;
                std::string users = "Online users\n";
                pthread_mutex_lock(&g_clients_mutex);
                for (const auto& c : g_clients)
                    users += std::string(c.nickname) + "\n";
                pthread_mutex_unlock(&g_clients_mutex);
                send_server_info(fd, src_ip, users.c_str());
                break;
            }

            case MSG_HISTORY: {
                std::cout << "[Application]    handle MSG_HISTORY" << std::endl;
                int count = 0;
                if (msg.payload[0] != '\0')
                    count = atoi(msg.payload);

                std::string result;
                pthread_mutex_lock(&g_history_mutex);
                int start = 0;
                if (count > 0 && (int)g_history.size() > count)
                    start = (int)g_history.size() - count;
                for (int i = start; i < (int)g_history.size(); i++) {
                    const auto& r = g_history[i];
                    char line[512];
                    if (r.type == MSG_TEXT) {
                        snprintf(line, sizeof(line), "[%s][id=%u][%s]: %s\n",
                                 format_time(r.timestamp).c_str(),
                                 r.msg_id, r.sender, r.text);
                    } else if (r.is_offline) {
                        snprintf(line, sizeof(line),
                                 "[%s][id=%u][%s -> %s][OFFLINE]: %s\n",
                                 format_time(r.timestamp).c_str(),
                                 r.msg_id, r.sender, r.receiver, r.text);
                    } else {
                        snprintf(line, sizeof(line),
                                 "[%s][id=%u][%s -> %s][PRIVATE]: %s\n",
                                 format_time(r.timestamp).c_str(),
                                 r.msg_id, r.sender, r.receiver, r.text);
                    }
                    result += line;
                }
                pthread_mutex_unlock(&g_history_mutex);
                if (result.empty()) result = "(no messages yet)";

                {
                    MessageEx hd = make_msg(MSG_HISTORY_DATA, "SERVER", msg.sender,
                                            result.c_str());
                    log_send(src_ip, "MSG_HISTORY_DATA");
                    send_ex(fd, hd);
                }
                std::cout << "[LOG]: i love TCP/IP (don't tell UDP)" << std::endl;
                break;
            }

            case MSG_BYE: {
                std::cout << "[Application]    handle MSG_BYE" << std::endl;
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

    std::cout << "[Application] SYN -> ACK -> READY" << std::endl;
    std::cout << "[Application] coffee powered TCP/IP stack initialized" << std::endl;
    std::cout << "[Application] packets never sleep" << std::endl;
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
