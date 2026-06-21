#include "varn/socket/SocketTransport.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace varn::socket
{

void SocketTransport::connectAsync(varn::runtime::Runtime& /*runtime*/, const std::string& /*host*/, int /*port*/, int /*timeoutMs*/, ConnectCallback callback)
{
    callback(nullptr, "[SocketTransport] The socket module is not available in this build.");
}

std::shared_ptr<TcpListener> SocketTransport::listen(varn::runtime::Runtime& /*runtime*/, const std::string& /*host*/, int /*port*/, int /*backlog*/)
{
    throw std::runtime_error("[SocketTransport] The socket module is not available in this build.");
}

std::shared_ptr<UdpSocket> SocketTransport::bindUdp(varn::runtime::Runtime& /*runtime*/, const std::string& /*host*/, int /*port*/)
{
    throw std::runtime_error("[SocketTransport] The socket module is not available in this build.");
}

} // namespace varn::socket
