#pragma once

#include "varn/http/HttpServerFactory.h"
#include "varn/http/StaticFileHandler.h"

#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPServerResponse.h>

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace varn {

class PocoHttpServer final : public HttpServer {
public:
    PocoHttpServer(Runtime& rt, HttpServerOptions opts, HttpHandler onRequest);
    ~PocoHttpServer() override;

    void start() override;
    void stop() override;

private:
    Runtime& runtime;
    HttpServerOptions serverOptions;
    HttpHandler httpHandler;
    std::unique_ptr<Poco::Net::HTTPServer> server;
};

class PocoDeferredResponse final : public HttpResponse {
public:
    void setStatus(int statusCode) override;
    void setHeader(const std::string& name, const std::string& value) override;
    void end(const std::string& body) override;
    bool ended() const override;

    void flushTo(Poco::Net::HTTPServerResponse& response);

private:
    mutable std::mutex sync;
    std::condition_variable ready;
    int statusCode = 200;
    std::map<std::string, std::string> headerMap;
    std::string payload;
    bool finished = false;
};

} // namespace varn
