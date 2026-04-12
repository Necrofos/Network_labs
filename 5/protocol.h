#pragma once

#include <cstdint>
#include <ctime>

#define MAX_NAME     32
#define MAX_PAYLOAD  256
#define PORT         8080
#define THREAD_POOL_SIZE 10

enum MsgType {
    MSG_HELLO        = 1,
    MSG_WELCOME      = 2,
    MSG_TEXT         = 3,
    MSG_PING         = 4,
    MSG_PONG         = 5,
    MSG_BYE          = 6,
    MSG_AUTH         = 7,
    MSG_PRIVATE      = 8,
    MSG_ERROR        = 9,
    MSG_SERVER_INFO  = 10,
    MSG_LIST         = 11,
    MSG_HISTORY      = 12,
    MSG_HISTORY_DATA = 13,
    MSG_HELP         = 14
};

#pragma pack(push, 1)
struct MessageEx {
    uint32_t length;               // длина payload
    uint8_t  type;                 // тип сообщения
    uint32_t msg_id;               // уникальный идентификатор
    char     sender[MAX_NAME];     // ник отправителя
    char     receiver[MAX_NAME];   // ник получателя ("" = broadcast)
    time_t   timestamp;            // время создания
    char     payload[MAX_PAYLOAD]; // текст / данные
};
#pragma pack(pop)
