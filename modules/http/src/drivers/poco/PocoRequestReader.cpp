#include "PocoRequestReader.h"

#include "varn/log/Log.h"

#include <Poco/Net/NameValueCollection.h>

#include <istream>
#include <sstream>

namespace varn::http {

HttpRequest PocoRequestReader::convert(Poco::Net::HTTPServerRequest& request, const HttpServerOptions& opts) {
    Poco::URI uri(request.getURI());

    HttpRequest out;
    out.host = request.getHost();
    out.method = request.getMethod();
    out.target = request.getURI();
    out.path = uri.getPath().empty() ? "/" : uri.getPath();
    out.queryString = uri.getRawQuery();
    out.headers = headers(request);
    out.cookies = cookies(request);
    out.query = query(uri);
    out.remoteAddress = request.clientAddress().toString();
    out.body = body(request, opts.maxRequestBodyBytes);

    return out;
}

std::map<std::string, std::string> PocoRequestReader::headers(const Poco::Net::HTTPRequest& request) {
    std::map<std::string, std::string> result;
    for (auto it = request.begin(); it != request.end(); ++it) {
        result[it->first] = it->second;
    }
    return result;
}

std::map<std::string, std::string> PocoRequestReader::query(const Poco::URI& uri) {
    std::map<std::string, std::string> result;
    for (const auto& param : uri.getQueryParameters()) {
        result[param.first] = param.second;
    }
    return result;
}

std::map<std::string, std::string> PocoRequestReader::cookies(const Poco::Net::HTTPServerRequest& request) {
    std::map<std::string, std::string> result;
    Poco::Net::NameValueCollection values;
    try {
        request.getCookies(values);
        for (auto it = values.begin(); it != values.end(); ++it) {
            result[it->first] = it->second;
        }
    } catch (const std::exception& ex) {
        log::Log::line("http", std::string("The cookie header was rejected. ") + ex.what());
    }
    return result;
}

std::string PocoRequestReader::body(Poco::Net::HTTPServerRequest& request, int maxBytes) {
    std::istream& input = request.stream();
    std::ostringstream out;
    char buffer[8192];
    std::size_t total = 0;

    while (input.good()) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize count = input.gcount();
        if (count <= 0) {
            break;
        }

        total += static_cast<std::size_t>(count);
        if (total > static_cast<std::size_t>(maxBytes)) {
            throw RequestBodyTooLarge();
        }

        out.write(buffer, count);
    }

    return out.str();
}

} // namespace varn::http
