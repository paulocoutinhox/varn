#include "varn/socket/SocketTransport.h"

#include <string>

namespace varn::socket
{

void SocketTransport::connectTlsAsync(varn::runtime::Runtime& /*runtime*/, const std::string& /*host*/, int /*port*/,
                                      int /*timeoutMs*/, bool /*verify*/, ConnectCallback callback)
{
    callback(nullptr, "[SocketTransport] TLS support was not enabled in this build.");
}

} // namespace varn::socket
