#include "varn/http/HttpClientPerform.h"

#include <stdexcept>

namespace varn::http::client {

std::string HttpClientPerform::perform(
    const std::string& /*method*/,
    const std::string& /*url*/,
    const std::map<std::string, std::string>& /*headers*/,
    const std::string& /*body*/,
    const ClientRequestOptions& /*options*/) {
    throw std::runtime_error("[DummyHttpClient] The HTTP client is not available in this build.");
}

} // namespace varn::http::client
