#pragma once

#include <cstdint>

#define MAX_PAYLOAD      1024
#define MAX_NICK         32
#define PORT             8080
#define THREAD_POOL_SIZE 10

enum MsgType {
    MSG_HELLO       = 1,
    MSG_WELCOME     = 2,
    MSG_TEXT        = 3,
    MSG_PING        = 4,
    MSG_PONG        = 5,
    MSG_BYE         = 6,
    MSG_AUTH        = 7,
    MSG_PRIVATE     = 8,
    MSG_ERROR       = 9,
    MSG_SERVER_INFO = 10
};

#pragma pack(push, 1)
struct Message {
    uint32_t length;
    uint8_t  type;
    char     payload[MAX_PAYLOAD];
};
#pragma pack(pop)
