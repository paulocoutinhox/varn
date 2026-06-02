#pragma once

#include "varn/http/HttpTypes.h"
#include "varn/http/StaticFileHandler.h"

#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace varn::runtime {
class Runtime;
}

namespace varn::http {

using WebSocketUpgrade =
    std::function<void(Poco::Net::HTTPServerRequest&, Poco::Net::HTTPServerResponse&, std::shared_ptr<std::atomic<bool>>)>;

class PocoHttpServer final : public HttpServer {
public:
    PocoHttpServer(varn::runtime::Runtime& rt, HttpServerOptions opts, HttpHandler onRequest, WebSocketUpgrade onUpgrade = {});
    ~PocoHttpServer() override;

    void start() override;
    void stop() override;

private:
    varn::runtime::Runtime& runtime;
    HttpServerOptions serverOptions;
    HttpHandler httpHandler;
    WebSocketUpgrade upgradeHandler;
    std::unique_ptr<Poco::Net::HTTPServer> server;
    std::shared_ptr<std::atomic<bool>> stopping = std::make_shared<std::atomic<bool>>(false);
};

class PocoDeferredResponse final : public HttpResponse {
public:
    explicit PocoDeferredResponse(std::shared_ptr<std::atomic<bool>> serverStopping, long long requestTimeoutMs = 0);

    void setStatus(int statusCode) override;
    void setHeader(const std::string& name, const std::string& value) override;
    void addHeader(const std::string& name, const std::string& value) override;
    void write(const std::string& chunk) override;
    void end(const std::string& body) override;
    bool ended() const override;
    void sendFile(const std::string& path, std::uint64_t start, std::uint64_t length, bool headersOnly) override;

    void flushTo(Poco::Net::HTTPServerResponse& response);

private:
    void writeHeaders(Poco::Net::HTTPServerResponse& response);
    void streamFile(Poco::Net::HTTPServerResponse& response, const std::string& path, std::uint64_t start,
                    std::uint64_t length, bool headersOnly);

    mutable std::mutex sync;
    std::condition_variable ready;
    std::shared_ptr<std::atomic<bool>> stopping;
    long long requestTimeoutMs = 0;
    int statusCode = 200;
    std::map<std::string, std::string> headerMap;
    std::vector<std::pair<std::string, std::string>> extraHeaders;
    std::deque<std::string> chunks;
    std::string payload;
    bool streaming = false;
    bool finished = false;
    bool fileMode = false;
    std::string filePath;
    std::uint64_t fileStart = 0;
    std::uint64_t fileLength = 0;
    bool fileHeadersOnly = false;
};

} // namespace varn::http
