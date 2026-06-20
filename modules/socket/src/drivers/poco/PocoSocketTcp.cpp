#include "varn/socket/SocketTransport.h"

#include "varn/runtime/EventLoop.h"
#include "varn/runtime/Runtime.h"

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace varn::socket
{

using varn::runtime::EventLoop;

namespace
{

constexpr int kMaxReceiveBytes = 16 * 1024 * 1024;

void closeManagedSocket(EventLoop& loop, const Poco::Net::Socket& socket)
{
    if (loop.isRunning())
    {
        loop.closeSocket(socket);
        return;
    }

    // the loop is already stopped at shutdown, so the fd can be closed here with no thread to race.
    try
    {
        socket.impl()->close();
    }
    catch (...)
    {
    }
}

class PocoTcpConnection : public TcpConnection, public std::enable_shared_from_this<PocoTcpConnection>
{
public:
    PocoTcpConnection(Poco::Net::StreamSocket socket, EventLoop& loop)
        : socket(std::move(socket))
        , loop(loop)
    {
        this->socket.setBlocking(false);
    }

    void receiveAsync(int maxBytes, ReceiveCallback callback) override
    {
        if (closed)
        {
            callback(false, "[PocoTcpConnection] The connection is closed.");
            return;
        }

        auto self = shared_from_this();
        loop.watchRead(socket, [self, maxBytes, callback = std::move(callback)]() -> bool
                       {
            try {
                const int capped = maxBytes < kMaxReceiveBytes ? maxBytes : kMaxReceiveBytes;
                std::vector<char> buffer(static_cast<std::size_t>(capped));
                const int received = self->socket.receiveBytes(buffer.data(), capped);
                if (received < 0) {
                    return false;
                }

                callback(true, received > 0 ? std::string(buffer.data(), static_cast<std::size_t>(received)) : std::string());
                return true;
            } catch (const std::exception& ex) {
                callback(false, ex.what());
                return true;
            } });
    }

    void sendAsync(std::string data, SendCallback callback) override
    {
        if (closed)
        {
            callback(false, "[PocoTcpConnection] The connection is closed.");
            return;
        }

        auto self = shared_from_this();
        auto payload = std::make_shared<std::string>(std::move(data));
        auto sent = std::make_shared<std::size_t>(0);
        loop.watchWrite(socket, [self, payload, sent, callback = std::move(callback)]() -> bool
                        {
            try {
                while (*sent < payload->size()) {
                    const std::size_t remaining = payload->size() - *sent;
                    const int chunk = static_cast<int>(std::min(remaining, static_cast<std::size_t>(INT_MAX)));
                    const int wrote = self->socket.sendBytes(payload->data() + *sent, chunk);
                    if (wrote < 0) {
                        return false;
                    }

                    if (wrote == 0) {
                        callback(false, "[PocoTcpConnection] The connection was closed before all data was sent.");
                        return true;
                    }

                    *sent += static_cast<std::size_t>(wrote);
                }

                callback(true, std::string());
                return true;
            } catch (const std::exception& ex) {
                callback(false, ex.what());
                return true;
            } });
    }

    void close() override
    {
        closed = true;
        closeManagedSocket(loop, socket);
    }

private:
    Poco::Net::StreamSocket socket;
    EventLoop& loop;
    bool closed = false;
};

class PocoTcpListener : public TcpListener, public std::enable_shared_from_this<PocoTcpListener>
{
public:
    PocoTcpListener(Poco::Net::ServerSocket server, EventLoop& loop)
        : server(std::move(server))
        , loop(loop)
    {
        this->server.setBlocking(false);
    }

    void acceptAsync(AcceptCallback callback) override
    {
        if (closed)
        {
            callback(nullptr, "[PocoTcpListener] The listener is closed.");
            return;
        }

        auto self = shared_from_this();
        loop.watchRead(server, [self, callback = std::move(callback)]() -> bool
                       {
            try {
                Poco::Net::StreamSocket accepted = self->server.acceptConnection();
                callback(std::make_shared<PocoTcpConnection>(std::move(accepted), self->loop), "");
                return true;
            } catch (const std::exception& ex) {
                callback(nullptr, ex.what());
                return true;
            } });
    }

    void close() override
    {
        closed = true;
        closeManagedSocket(loop, server);
    }

private:
    Poco::Net::ServerSocket server;
    EventLoop& loop;
    bool closed = false;
};

} // namespace

void SocketTransport::connectAsync(varn::runtime::Runtime& runtime, const std::string& host, int port, ConnectCallback callback)
{
    EventLoop& loop = runtime.mainLoop();

    Poco::Net::StreamSocket socket;
    try
    {
        socket.connectNB(Poco::Net::SocketAddress(host, static_cast<Poco::UInt16>(port)));
    }
    catch (const std::exception& ex)
    {
        callback(nullptr, ex.what());
        return;
    }

    socket.setBlocking(false);

    loop.watchWrite(socket, [&loop, socket, callback = std::move(callback)]() mutable -> bool
                    {
        int error = 0;
        try {
            error = socket.impl()->socketError();
        } catch (...) {
            error = -1;
        }

        if (error != 0) {
            callback(nullptr, "[SocketTransport] The connection could not be established.");
            return true;
        }

        callback(std::make_shared<PocoTcpConnection>(socket, loop), "");
        return true; });
}

std::shared_ptr<TcpListener> SocketTransport::listen(varn::runtime::Runtime& runtime, const std::string& host, int port, int backlog)
{
    Poco::Net::ServerSocket server(Poco::Net::SocketAddress(host, static_cast<Poco::UInt16>(port)), backlog);
    return std::make_shared<PocoTcpListener>(std::move(server), runtime.mainLoop());
}

} // namespace varn::socket
