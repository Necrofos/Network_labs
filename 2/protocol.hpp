#pragma once
#include <cstdint>
#include <string>

constexpr std::size_t MAX_PAYLOAD = 1024;

enum : std::uint8_t
{
    MSG_HELLO   = 1,
    MSG_WELCOME = 2,
    MSG_TEXT    = 3,
    MSG_PING    = 4,
    MSG_PONG    = 5,
    MSG_BYE     = 6
};

struct Message
{
    std::uint8_t type{};
    std::string payload;
};