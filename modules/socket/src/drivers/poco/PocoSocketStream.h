#pragma once

#include "varn/socket/SocketTransport.h"

#include "varn/runtime/EventLoop.h"

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/Socket.h>
#include <Poco/Net/StreamSocket.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace varn::socket
{

constexpr int kMaxReceiveBytes = 16 * 1024 * 1024;

class ManagedSocket
{
public:
    ManagedSocket() = delete;

    static void close(varn::runtime::EventLoop& loop, const Poco::Net::Socket& socket)
    {
        if (loop.isRunning())
        {
            loop.closeSocket(socket);
            return;
        }

        // closes the fd directly when the loop is stopped at shutdown
        try
        {
            socket.impl()->close();
        }
        catch (...)
        {
        }
    }
};

class PocoStreamConnection : public TcpConnection, public std::enable_shared_from_this<PocoStreamConnection>
{
public:
    PocoStreamConnection(Poco::Net::StreamSocket socket, varn::runtime::EventLoop& loop)
        : socket(std::move(socket))
        , loop(loop)
    {
        this->socket.setBlocking(false);
    }

    void receiveAsync(int maxBytes, ReceiveCallback callback) override
    {
        if (closed)
        {
            callback(false, "[PocoStreamConnection] The connection is closed.");
            return;
        }

        auto self = shared_from_this();
        loop.watchRead(socket, [self, maxBytes, callback = std::move(callback)]() -> bool
                       {
            try
            {
                const int capped = maxBytes < kMaxReceiveBytes ? maxBytes : kMaxReceiveBytes;
                std::vector<char> buffer(static_cast<std::size_t>(capped));
                const int received = self->socket.receiveBytes(buffer.data(), capped);
                if (received < 0)
                {
                    return false;
                }

                callback(true, received > 0 ? std::string(buffer.data(), static_cast<std::size_t>(received)) : std::string());
                return true;
            }
            catch (const std::exception& ex)
            {
                callback(false, ex.what());
                return true;
            } });
    }

    void sendAsync(std::string data, SendCallback callback) override
    {
        if (closed)
        {
            callback(false, "[PocoStreamConnection] The connection is closed.");
            return;
        }

        auto self = shared_from_this();
        auto payload = std::make_shared<std::string>(std::move(data));
        auto sent = std::make_shared<std::size_t>(0);
        loop.watchWrite(socket, [self, payload, sent, callback = std::move(callback)]() -> bool
                        {
            try
            {
                while (*sent < payload->size())
                {
                    const std::size_t remaining = payload->size() - *sent;
                    const int chunk = static_cast<int>(std::min(remaining, static_cast<std::size_t>(INT_MAX)));
                    const int wrote = self->socket.sendBytes(payload->data() + *sent, chunk);
                    if (wrote < 0)
                    {
                        return false;
                    }

                    if (wrote == 0)
                    {
                        callback(false, "[PocoStreamConnection] The connection was closed before all data was sent.");
                        return true;
                    }

                    *sent += static_cast<std::size_t>(wrote);
                }

                callback(true, std::string());
                return true;
            }
            catch (const std::exception& ex)
            {
                callback(false, ex.what());
                return true;
            } });
    }

    void close() override
    {
        closed = true;
        ManagedSocket::close(loop, socket);
    }

private:
    Poco::Net::StreamSocket socket;
    varn::runtime::EventLoop& loop;
    bool closed = false;
};

class PocoStreamListener : public TcpListener, public std::enable_shared_from_this<PocoStreamListener>
{
public:
    PocoStreamListener(Poco::Net::ServerSocket server, varn::runtime::EventLoop& loop, std::string closedError)
        : server(std::move(server))
        , loop(loop)
        , closedError(std::move(closedError))
    {
        this->server.setBlocking(false);
    }

    void acceptAsync(AcceptCallback callback) override
    {
        if (closed)
        {
            callback(nullptr, closedError);
            return;
        }

        auto self = shared_from_this();
        loop.watchRead(server, [self, callback = std::move(callback)]() -> bool
                       {
            try
            {
                Poco::Net::StreamSocket accepted = self->server.acceptConnection();
                callback(std::make_shared<PocoStreamConnection>(std::move(accepted), self->loop), "");
                return true;
            }
            catch (const std::exception& ex)
            {
                callback(nullptr, ex.what());
                return true;
            } });
    }

    void close() override
    {
        closed = true;
        ManagedSocket::close(loop, server);
    }

private:
    Poco::Net::ServerSocket server;
    varn::runtime::EventLoop& loop;
    std::string closedError;
    bool closed = false;
};

} // namespace varn::socket
