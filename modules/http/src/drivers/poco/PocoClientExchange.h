#pragma once

#include "varn/http/HttpClientPerform.h"

#include <Poco/Net/HTTPRequest.h>
#include <Poco/URI.h>

#include <istream>
#include <map>
#include <string>

#if defined(VARN_ENABLE_TLS)
#include <Poco/Net/Context.h>
#endif

namespace varn::http::client {

class PocoClientExchange {
public:
    static std::string performHttp(const std::string& method, const Poco::URI& uri,
                                   const std::map<std::string, std::string>& headers, const std::string& body,
                                   const ClientRequestOptions& options);
#if defined(VARN_ENABLE_TLS)
    static std::string performHttps(const std::string& method, const Poco::URI& uri,
                                    const std::map<std::string, std::string>& headers, const std::string& body,
                                    const ClientRequestOptions& options);
#endif

private:
    static std::string buildWire(int status, const std::string& body);
    static bool headerControlSafe(const std::string& value);
    static void applyHeaders(Poco::Net::HTTPRequest& request, const std::map<std::string, std::string>& headers);
    static std::string readResponseBody(std::istream& rs, std::size_t maxBytes);
#if defined(VARN_ENABLE_TLS)
    static Poco::Net::Context::Ptr tlsClientContext(bool verify);
    static void ensureTlsClientInitialized();
#endif
};

} // namespace varn::http::client
