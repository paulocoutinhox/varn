#include "varn/socket/SocketTransport.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace varn::socket {

std::shared_ptr<TcpConnection> connectBlocking(const std::string& /*host*/, int /*port*/) {
    throw std::runtime_error(
        "TCP sockets are not available in this build (VARN_SOCKET_DRIVER=" VARN_SOCKET_DRIVER_NAME "). "
        "Select VARN_SOCKET_DRIVER=POCO in CMake when the toolchain supports Poco Net; see docs/build.md.");
}

std::shared_ptr<TcpListener> listenBlocking(const std::string& /*host*/, int /*port*/, int /*backlog*/) {
    throw std::runtime_error(
        "TCP sockets are not available in this build (VARN_SOCKET_DRIVER=" VARN_SOCKET_DRIVER_NAME "). "
        "Select VARN_SOCKET_DRIVER=POCO in CMake when the toolchain supports Poco Net; see docs/build.md.");
}

} // namespace varn::socket
