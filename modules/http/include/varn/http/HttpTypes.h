#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace varn::http {

struct HttpRequest {
    std::string host;
    std::string method;
    std::string path;
    std::string target;
    std::string queryString;
    std::string body;
    std::string remoteAddress;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> cookies;
    std::map<std::string, std::string> query;
};

class HttpResponse {
public:
    virtual ~HttpResponse() = default;
    virtual void setStatus(int statusCode) = 0;
    virtual void setHeader(const std::string& name, const std::string& value) = 0;
    virtual void addHeader(const std::string& name, const std::string& value) = 0;
    virtual void write(const std::string& chunk) = 0;
    virtual void end(const std::string& body) = 0;
    virtual bool ended() const = 0;
    virtual void sendFile(const std::string& path, std::uint64_t start, std::uint64_t length, bool headersOnly);
};

class HttpServer {
public:
    virtual ~HttpServer() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
};

struct HttpServerOptions {
    std::string host = "0.0.0.0";
    int port = 3000;
    bool tls = false;
    std::string certFile;
    std::string keyFile;
    std::string publicDir = "apps/lua/public";
    bool servePublic = true;
    bool directoryListing = false;
    int maxQueued = 65536;
    int maxThreads = 0;
    int keepAliveTimeoutSeconds = 30;
    long long maxRequestBodyBytes = 16 * 1024 * 1024;
    long long requestTimeoutMs = 30000;
};

using HttpHandler = std::function<void(const HttpRequest&, std::shared_ptr<HttpResponse>)>;

} // namespace varn::http
