#pragma once

#include <memory>
#include <string>

namespace varn::socket {

class TcpConnection {
public:
    virtual ~TcpConnection() = default;

    virtual void sendBlocking(const std::string& data) = 0;
    virtual std::string receiveBlocking(int maxBytes) = 0;
    virtual void closeBlocking() = 0;
};

class TcpListener {
public:
    virtual ~TcpListener() = default;

    virtual std::shared_ptr<TcpConnection> acceptBlocking() = 0;
    virtual void closeBlocking() = 0;
};

struct UdpDatagram {
    std::string data;
    std::string host;
    int port = 0;
};

class UdpSocket {
public:
    virtual ~UdpSocket() = default;

    virtual void sendToBlocking(const std::string& host, int port, const std::string& data) = 0;
    virtual UdpDatagram receiveFromBlocking(int maxBytes) = 0;
    virtual void closeBlocking() = 0;
};

class SocketTransport {
public:
    SocketTransport() = delete;

    static std::shared_ptr<TcpConnection> connectBlocking(const std::string& host, int port);
    static std::shared_ptr<TcpListener> listenBlocking(const std::string& host, int port, int backlog);
    static std::shared_ptr<UdpSocket> bindUdpBlocking(const std::string& host, int port);

private:
    static void checkPort(int port);
    static void checkBacklog(int backlog);
};

} // namespace varn::socket
