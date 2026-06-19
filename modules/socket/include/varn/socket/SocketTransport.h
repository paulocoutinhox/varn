#pragma once

#include <memory>
#include <string>

namespace varn::socket {

// closeBlocking interrupts any in-flight blocking operation, so the runtime can release a socket at shutdown before joining its workers.
class SocketHandle {
public:
    virtual ~SocketHandle() = default;

    virtual void closeBlocking() = 0;
};

class TcpConnection : public SocketHandle {
public:
    virtual void sendBlocking(const std::string& data) = 0;
    virtual std::string receiveBlocking(int maxBytes) = 0;
};

class TcpListener : public SocketHandle {
public:
    virtual std::shared_ptr<TcpConnection> acceptBlocking() = 0;
};

struct UdpDatagram {
    std::string data;
    std::string host;
    int port = 0;
};

class UdpSocket : public SocketHandle {
public:
    virtual void sendToBlocking(const std::string& host, int port, const std::string& data) = 0;
    virtual UdpDatagram receiveFromBlocking(int maxBytes) = 0;
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
