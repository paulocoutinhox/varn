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

std::shared_ptr<TcpConnection> connectBlocking(const std::string& host, int port);

std::shared_ptr<TcpListener> listenBlocking(const std::string& host, int port, int backlog);

} // namespace varn::socket
