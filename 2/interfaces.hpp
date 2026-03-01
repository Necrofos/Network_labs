#pragma once

class IServer
{
public:
    virtual ~IServer() = default;
    virtual void start() = 0;
};

class IClient
{
public:
    virtual ~IClient() = default;
    virtual void start() = 0;
};