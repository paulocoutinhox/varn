#include "varn/socket/SocketTransport.h"

#include "PocoSocketStream.h"

#include "varn/runtime/EventLoop.h"
#include "varn/runtime/Runtime.h"

#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>

#include <exception>
#include <memory>
#include <string>

namespace varn::socket
{

using varn::runtime::EventLoop;

namespace
{

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
        closeManagedSocket(loop, server);
    }

private:
    Poco::Net::ServerSocket server;
    EventLoop& loop;
    bool closed = false;
};

} // namespace

void SocketTransport::connectAsync(varn::runtime::Runtime& runtime, const std::string& host, int port, int timeoutMs, ConnectCallback callback)
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

    // the write watcher and the optional timeout race to settle the connect, so the first to fire wins and the other becomes a no-op.
    auto settled = std::make_shared<bool>(false);
    auto shared = std::make_shared<ConnectCallback>(std::move(callback));

    loop.watchWrite(socket, [&loop, socket, settled, shared]() mutable -> bool
                    {
        if (*settled)
        {
            return true;
        }

        *settled = true;

        int error = 0;
        try
        {
            error = socket.impl()->socketError();
        }
        catch (...)
        {
            error = -1;
        }

        if (error != 0)
        {
            (*shared)(nullptr, "[SocketTransport] The connection could not be established.");
            return true;
        }

        (*shared)(std::make_shared<PocoStreamConnection>(socket, loop), "");
        return true; });

    if (timeoutMs > 0)
    {
        loop.postDelayed(timeoutMs, [&loop, socket, settled, shared]() mutable
                         {
            if (*settled)
            {
                return;
            }

            *settled = true;
            loop.closeSocket(socket);
            (*shared)(nullptr, "[SocketTransport] The connection timed out."); });
    }
}

std::shared_ptr<TcpListener> SocketTransport::listen(varn::runtime::Runtime& runtime, const std::string& host, int port, int backlog)
{
    Poco::Net::ServerSocket server(Poco::Net::SocketAddress(host, static_cast<Poco::UInt16>(port)), backlog);
    return std::make_shared<PocoTcpListener>(std::move(server), runtime.mainLoop());
}

} // namespace varn::socket
