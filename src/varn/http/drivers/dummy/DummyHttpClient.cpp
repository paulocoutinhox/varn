#include "varn/http/HttpClientPerform.h"

#include <stdexcept>

namespace varn::http::client {

std::string performRequestWire(
    const std::string& /*method*/,
    const std::string& /*url*/,
    const std::map<std::string, std::string>& /*headers*/,
    const std::string& /*body*/,
    int /*timeoutSeconds*/) {
    throw std::runtime_error("HTTP client is not available in this build (VARN_HTTP_CLIENT_DRIVER="
        VARN_HTTP_CLIENT_DRIVER_NAME ").");
}

} // namespace varn::http::client
