#include "varn/socket/SocketTransport.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace varn::socket {

std::shared_ptr<TcpConnection> SocketTransport::connectBlocking(const std::string& /*host*/, int /*port*/) {
    throw std::runtime_error("[socket] The socket module is not available in this build.");
}

std::shared_ptr<TcpListener> SocketTransport::listenBlocking(const std::string& /*host*/, int /*port*/, int /*backlog*/) {
    throw std::runtime_error("[socket] The socket module is not available in this build.");
}

} // namespace varn::socket
