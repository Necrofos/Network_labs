#include "net.hpp"

TcpSocket::TcpSocket(int fd) : fd_(fd) {}

TcpSocket::TcpSocket(TcpSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }

TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept
{
    if (this != &o)
    {
        close();
        fd_ = o.fd_;
        o.fd_ = -1;
    }
    return *this;
}

TcpSocket::~TcpSocket() { close(); }

int TcpSocket::fd() const { return fd_; }
bool TcpSocket::valid() const { return fd_ >= 0; }

void TcpSocket::close()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

void TcpSocket::shutdownBoth()
{
    if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);
}

TcpSocket TcpSocket::create()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw SysError("socket");
    return TcpSocket(fd);
}

void TcpSocket::setReuseAddr()
{
    int opt = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

void TcpSocket::bindAny(std::uint16_t port)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw SysError("bind");
}

void TcpSocket::listen(int backlog)
{
    if (::listen(fd_, backlog) < 0)
        throw SysError("listen");
}

TcpSocket TcpSocket::accept(sockaddr_in* outAddr)
{
    sockaddr_in caddr{};
    socklen_t clen = sizeof(caddr);
    int cfd = ::accept(fd_, reinterpret_cast<sockaddr*>(&caddr), &clen);
    if (cfd < 0) throw SysError("accept");
    if (outAddr) *outAddr = caddr;
    return TcpSocket(cfd);
}

void TcpSocket::connectTo(const std::string& hostIp, std::uint16_t port)
{
    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(port);

    if (::inet_pton(AF_INET, hostIp.c_str(), &srv.sin_addr) != 1)
        throw std::runtime_error("Bad host ip");

    if (::connect(fd_, reinterpret_cast<sockaddr*>(&srv), sizeof(srv)) < 0)
        throw SysError("connect");
}

void TcpSocket::sendAll(const void* data, std::size_t n)
{
    const char* p = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < n)
    {
        ssize_t s = ::send(fd_, p + sent, n - sent, 0);
        if (s < 0)
        {
            if (errno == EINTR) continue;
            throw SysError("send");
        }
        sent += static_cast<std::size_t>(s);
    }
}

bool TcpSocket::recvAll(void* data, std::size_t n)
{
    char* p = static_cast<char*>(data);
    std::size_t got = 0;
    while (got < n)
    {
        ssize_t r = ::recv(fd_, p + got, n - got, 0);
        if (r == 0) return false;
        if (r < 0)
        {
            if (errno == EINTR) continue;
            throw SysError("recv");
        }
        got += static_cast<std::size_t>(r);
    }
    return true;
}

void TcpSocket::sendMessage(const Message& msg)
{
    if (msg.payload.size() > MAX_PAYLOAD)
        throw std::runtime_error("Payload too large");

    std::uint32_t len = 1 + static_cast<std::uint32_t>(msg.payload.size());
    std::uint32_t netlen = htonl(len);

    sendAll(&netlen, sizeof(netlen));
    sendAll(&msg.type, 1);

    if (!msg.payload.empty())
        sendAll(msg.payload.data(), msg.payload.size());
}

bool TcpSocket::recvMessage(Message& msg)
{
    std::uint32_t netlen = 0;
    if (!recvAll(&netlen, sizeof(netlen)))
        return false;

    std::uint32_t len = ntohl(netlen);
    if (len < 1 || len > 1 + MAX_PAYLOAD)
        throw std::runtime_error("Protocol error");

    std::uint8_t type = 0;
    if (!recvAll(&type, 1))
        return false;

    std::uint32_t payloadLen = len - 1;

    std::string payload;
    payload.resize(payloadLen);

    if (payloadLen > 0)
        if (!recvAll(payload.data(), payloadLen))
            return false;

    msg.type = type;
    msg.payload = std::move(payload);
    return true;
}

std::string addrToString(const sockaddr_in& a)
{
    char ip[INET_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}