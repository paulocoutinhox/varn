#pragma once

#include "varn/http/HttpTypes.h"

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/URI.h>

#include <map>
#include <stdexcept>
#include <string>

namespace varn::http {

struct RequestBodyTooLarge : std::runtime_error {
    RequestBodyTooLarge() : std::runtime_error("[PocoRequestReader] The request body is too large.") {}
};

class PocoRequestReader {
public:
    static HttpRequest convert(Poco::Net::HTTPServerRequest& request, const HttpServerOptions& opts);

private:
    static std::map<std::string, std::string> headers(const Poco::Net::HTTPRequest& request);
    static std::map<std::string, std::string> query(const Poco::URI& uri);
    static std::map<std::string, std::string> cookies(const Poco::Net::HTTPServerRequest& request);
    static std::string body(Poco::Net::HTTPServerRequest& request, long long maxBytes);
};

} // namespace varn::http
