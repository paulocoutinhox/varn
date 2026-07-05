#pragma once

#include "varn/socket/SocketTransport.h"

#include "varn/runtime/EventLoop.h"
#include "varn/runtime/Runtime.h"
#include "varn/runtime/TaskPool.h"

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/Socket.h>
#include <Poco/Net/StreamSocket.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
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

        // a tls socket cannot be driven by the loop readiness poll, since on windows the schannel i/o races the uv_poll re-arm and drops events, so read blocking on the io pool and settle back on the loop
        if (secure)
        {
            receiveSecure(maxBytes, std::move(callback));
            return;
        }

        watchReadOnce(maxBytes, std::move(callback));
    }

    void sendAsync(std::string data, SendCallback callback) override
    {
        if (closed)
        {
            callback(false, "[PocoStreamConnection] The connection is closed.");
            return;
        }

        auto self = shared_from_this();

        // secure writes go blocking on the io pool for the same reason as reads, keeping schannel off the loop readiness poll
        if (secure)
        {
            sendSecure(std::move(data), std::move(callback));
            return;
        }

        auto payload = std::make_shared<std::string>(std::move(data));
        auto sent = std::make_shared<std::size_t>(0);
        // clang-format off
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
            }
        });
        // clang-format on
    }

    void close() override
    {
        closed = true;
        ManagedSocket::close(loop, socket);
    }

    void startTlsAsync(varn::runtime::Runtime& runtime, std::string host, bool verify, SendCallback callback) override;

    // swaps the plaintext transport for the handshaked secure socket so later reads and writes run blocking over tls
    void adoptSecure(const Poco::Net::StreamSocket& upgraded, varn::runtime::Runtime& rt)
    {
        socket = upgraded;
        enterSecure(rt);
    }

    // marks a connection that was already established over tls so its i/o runs blocking on the pool
    void markSecure(varn::runtime::Runtime& rt) { enterSecure(rt); }

private:
    void enterSecure(varn::runtime::Runtime& rt)
    {
        secure = true;
        runtime = &rt;
        socket.setBlocking(true);
    }

    // reads one chunk blocking on the io pool and settles the callback back on the loop thread
    void receiveSecure(int maxBytes, ReceiveCallback callback)
    {
        auto self = shared_from_this();
        // clang-format off
        runtime->ioPool().post([self, maxBytes, callback = std::move(callback)]() mutable
        {
            bool ok = false;
            std::string data;
            std::string error;
            try
            {
                const int capped = maxBytes < kMaxReceiveBytes ? maxBytes : kMaxReceiveBytes;
                std::vector<char> buffer(static_cast<std::size_t>(capped));
                const int received = self->socket.receiveBytes(buffer.data(), capped);
                ok = true;
                if (received > 0)
                {
                    data.assign(buffer.data(), static_cast<std::size_t>(received));
                }
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
            }

            self->loop.post([self, ok, data, error, callback = std::move(callback)]() mutable
            {
                callback(ok, ok ? data : error);
            });
        });
        // clang-format on
    }

    // writes the whole payload blocking on the io pool and settles the callback back on the loop thread
    void sendSecure(std::string data, SendCallback callback)
    {
        auto self = shared_from_this();
        auto payload = std::make_shared<std::string>(std::move(data));
        // clang-format off
        runtime->ioPool().post([self, payload, callback = std::move(callback)]() mutable
        {
            bool ok = false;
            std::string error;
            try
            {
                std::size_t sent = 0;
                while (sent < payload->size())
                {
                    const std::size_t remaining = payload->size() - sent;
                    const int chunk = static_cast<int>(std::min(remaining, static_cast<std::size_t>(INT_MAX)));
                    const int wrote = self->socket.sendBytes(payload->data() + sent, chunk);
                    if (wrote <= 0)
                    {
                        throw std::runtime_error("[PocoStreamConnection] The connection was closed before all data was sent.");
                    }

                    sent += static_cast<std::size_t>(wrote);
                }

                ok = true;
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
            }

            self->loop.post([self, ok, error, callback = std::move(callback)]() mutable
            {
                callback(ok, ok ? std::string() : error);
            });
        });
        // clang-format on
    }

    // performs a single read and returns whether the callback was invoked, false meaning the read would block
    bool readOnce(int maxBytes, const ReceiveCallback& callback)
    {
        try
        {
            const int capped = maxBytes < kMaxReceiveBytes ? maxBytes : kMaxReceiveBytes;
            std::vector<char> buffer(static_cast<std::size_t>(capped));
            const int received = socket.receiveBytes(buffer.data(), capped);
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
        }
    }

    void watchReadOnce(int maxBytes, ReceiveCallback callback)
    {
        auto self = shared_from_this();
        // clang-format off
        loop.watchRead(socket, [self, maxBytes, callback = std::move(callback)]() -> bool
        {
            return self->readOnce(maxBytes, callback);
        });
        // clang-format on
    }

    Poco::Net::StreamSocket socket;
    varn::runtime::EventLoop& loop;
    varn::runtime::Runtime* runtime = nullptr;
    bool closed = false;
    bool secure = false;
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
        // clang-format off
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
            }
        });
        // clang-format on
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
