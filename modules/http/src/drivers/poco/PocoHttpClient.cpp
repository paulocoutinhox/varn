#include "varn/http/HttpClientPerform.h"

#include "PocoClientExchange.h"

#include <Poco/URI.h>

#include <map>
#include <stdexcept>
#include <string>

namespace varn::http::client
{

std::string HttpClientPerform::perform(
    const std::string& method,
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const ClientRequestOptions& options)
{
    Poco::URI uri(url);
    const std::string scheme = uri.getScheme();
    if (scheme != "http" && scheme != "https")
    {
        throw std::runtime_error("[PocoHttpClient] The URL scheme must be http or https.");
    }

    if (scheme == "https")
    {
#if defined(VARN_ENABLE_TLS)
        return PocoClientExchange::performHttps(method, uri, headers, body, options);
#else
        throw std::runtime_error("[PocoHttpClient] Secure URLs require a build with TLS support enabled.");
#endif
    }

    return PocoClientExchange::performHttp(method, uri, headers, body, options);
}

} // namespace varn::http::client
