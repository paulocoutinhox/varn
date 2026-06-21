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

class PocoUnixListener : public TcpListener, public std::enable_shared_from_this<PocoUnixListener>
{
public:
    PocoUnixListener(Poco::Net::ServerSocket server, EventLoop& loop)
        : server(std::move(server))
        , loop(loop)
    {
        this->server.setBlocking(false);
    }

    void acceptAsync(AcceptCallback callback) override
    {
        if (closed)
        {
            callback(nullptr, "[PocoUnixListener] The listener is closed.");
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

void SocketTransport::connectUnixAsync(varn::runtime::Runtime& runtime, const std::string& path, ConnectCallback callback)
{
    EventLoop& loop = runtime.mainLoop();

    Poco::Net::StreamSocket socket;
    try
    {
        const Poco::Net::SocketAddress address(Poco::Net::SocketAddress::UNIX_LOCAL, path);
        socket.connectNB(address);
    }
    catch (const std::exception& ex)
    {
        callback(nullptr, ex.what());
        return;
    }

    socket.setBlocking(false);

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
}

std::shared_ptr<TcpListener> SocketTransport::listenUnix(varn::runtime::Runtime& runtime, const std::string& path, int backlog)
{
    const Poco::Net::SocketAddress address(Poco::Net::SocketAddress::UNIX_LOCAL, path);
    Poco::Net::ServerSocket server(address, backlog);
    return std::make_shared<PocoUnixListener>(std::move(server), runtime.mainLoop());
}

} // namespace varn::socket
