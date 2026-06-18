#include "varn/log/Log.h"
#include "varn/http/drivers/poco/PocoHttpServer.h"
#include "varn/runtime/Runtime.h"

#include "PocoRequestReader.h"
#include "TlsServerContext.h"

#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>

#ifdef VARN_ENABLE_TLS
#include <Poco/Net/Context.h>
#include <Poco/Net/SecureServerSocket.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>

namespace varn::http {

using varn::runtime::Runtime;

class VarnRequestHandler final : public Poco::Net::HTTPRequestHandler {
public:
    VarnRequestHandler(HttpServerOptions opts, HttpHandler onRequest, WebSocketUpgrade onUpgrade, std::shared_ptr<std::atomic<bool>> stopping)
        : httpOptions(std::move(opts)), onRequest(std::move(onRequest)), upgradeHandler(std::move(onUpgrade)), stopping(std::move(stopping)),
          publicFiles(httpOptions.publicDir, httpOptions.directoryListing) {}

    void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override {
        try {
            // hand websocket upgrade requests to the app's session handler.
            if (upgradeHandler && Poco::icompare(request.get("Upgrade", ""), "websocket") == 0) {
                upgradeHandler(request, response, stopping);
                return;
            }

            auto deferredResponse = std::make_shared<PocoDeferredResponse>(stopping, httpOptions.requestTimeoutMs);
            HttpRequest inboundRequest = PocoRequestReader::convert(request, httpOptions);

            if (httpOptions.servePublic && publicFiles.tryServe(inboundRequest, *deferredResponse)) {
                deferredResponse->flushTo(response);
                return;
            }

            onRequest(inboundRequest, deferredResponse);

            // write the buffered response after the handler returns while the library still owns the stream.
            deferredResponse->flushTo(response);
        } catch (const RequestBodyTooLarge& ex) {
            log::Log::error("VarnRequestHandler", ex.what());
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_REQUEST_ENTITY_TOO_LARGE);
            response.setContentType("text/plain; charset=utf-8");
            response.send() << "Payload too large.";
        } catch (const std::exception& ex) {
            // log the detail but never echo internal errors back to the client.
            log::Log::error("VarnRequestHandler", std::string("A request failed unexpectedly. ") + ex.what());
            response.setStatus(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
            response.setContentType("text/plain; charset=utf-8");
            response.send() << "Internal server error.";
        }
    }

private:
    HttpServerOptions httpOptions;
    HttpHandler onRequest;
    WebSocketUpgrade upgradeHandler;
    std::shared_ptr<std::atomic<bool>> stopping;
    StaticFileHandler publicFiles;
};

class VarnRequestHandlerFactory final : public Poco::Net::HTTPRequestHandlerFactory {
public:
    VarnRequestHandlerFactory(HttpServerOptions opts, HttpHandler onRequest, WebSocketUpgrade onUpgrade, std::shared_ptr<std::atomic<bool>> stopping)
        : httpOptions(std::move(opts)), onRequest(std::move(onRequest)), upgradeHandler(std::move(onUpgrade)), stopping(std::move(stopping)) {}

    Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest&) override {
        return new VarnRequestHandler(httpOptions, onRequest, upgradeHandler, stopping);
    }

private:
    HttpServerOptions httpOptions;
    HttpHandler onRequest;
    WebSocketUpgrade upgradeHandler;
    std::shared_ptr<std::atomic<bool>> stopping;
};

PocoHttpServer::PocoHttpServer(Runtime& rt, HttpServerOptions opts, HttpHandler onRequest, WebSocketUpgrade onUpgrade)
    : runtime(rt), serverOptions(std::move(opts)), httpHandler(std::move(onRequest)), upgradeHandler(std::move(onUpgrade)) {}

PocoHttpServer::~PocoHttpServer() {
    stop();
}

void PocoHttpServer::start() {
    stopping->store(false, std::memory_order_release);

    auto params = Poco::Net::HTTPServerParams::Ptr(new Poco::Net::HTTPServerParams());
    params->setMaxQueued(serverOptions.maxQueued);
    if (serverOptions.maxThreads > 0) {
        params->setMaxThreads(serverOptions.maxThreads);
    }
    params->setKeepAlive(true);
    params->setKeepAliveTimeout(Poco::Timespan(serverOptions.keepAliveTimeoutSeconds, 0));
    // bound how long a connection may take to send its request line and headers, blunting slowloris.
    params->setTimeout(Poco::Timespan(serverOptions.keepAliveTimeoutSeconds, 0));

    auto factory = Poco::Net::HTTPRequestHandlerFactory::Ptr(
        new VarnRequestHandlerFactory(serverOptions, httpHandler, upgradeHandler, stopping)
    );

    const Poco::Net::SocketAddress socketAddress(serverOptions.host, static_cast<Poco::UInt16>(serverOptions.port));
    const int listenBacklog = std::clamp(serverOptions.maxQueued, 1, 65535);

#ifdef VARN_ENABLE_TLS
    if (serverOptions.tls) {
        Poco::Net::Context::Ptr tlsContext = TlsServerContext::create(serverOptions);
        TlsServerContext::initializeSslManager(tlsContext);

        const Poco::Net::SecureServerSocket listeningSocket(socketAddress, listenBacklog, tlsContext);
        server = std::make_unique<Poco::Net::HTTPServer>(factory, listeningSocket, params);
        server->start();
        return;
    }
#endif

    Poco::Net::ServerSocket listeningSocket(socketAddress, listenBacklog);
    server = std::make_unique<Poco::Net::HTTPServer>(factory, listeningSocket, params);
    server->start();
}

void PocoHttpServer::stop() {
    if (server) {
        // wake any worker blocked on a deferred response so stop can join instead of hanging on an unfinished request.
        stopping->store(true, std::memory_order_release);
        server->stop();
        server.reset();
    }
}

PocoDeferredResponse::PocoDeferredResponse(std::shared_ptr<std::atomic<bool>> serverStopping, long long requestTimeoutMs)
    : stopping(std::move(serverStopping)), requestTimeoutMs(requestTimeoutMs) {}

void PocoDeferredResponse::setStatus(int code) {
    std::lock_guard<std::mutex> lock(sync);
    statusCode = code;
}

// drops every control octet, not just cr and lf, to prevent header smuggling.
std::string PocoDeferredResponse::sanitizeHeader(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u != 0x7f) {
            out += c;
        }
    }
    return out;
}

void PocoDeferredResponse::setHeader(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(sync);
    headerMap[sanitizeHeader(name)] = sanitizeHeader(value);
}

void PocoDeferredResponse::addHeader(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(sync);
    extraHeaders.emplace_back(sanitizeHeader(name), sanitizeHeader(value));
}

void PocoDeferredResponse::write(const std::string& chunk) {
    {
        std::lock_guard<std::mutex> lock(sync);
        if (finished) {
            return;
        }
        streaming = true;
        chunks.push_back(chunk);
    }
    ready.notify_all();
}

void PocoDeferredResponse::end(const std::string& body) {
    {
        std::lock_guard<std::mutex> lock(sync);
        if (finished) {
            return;
        }
        if (streaming) {
            if (!body.empty()) {
                chunks.push_back(body);
            }
        } else {
            payload = body;
        }
        finished = true;
    }
    ready.notify_all();
}

bool PocoDeferredResponse::ended() const {
    std::lock_guard<std::mutex> lock(sync);
    return finished;
}

void PocoDeferredResponse::sendFile(const std::string& path, std::uint64_t start, std::uint64_t length, bool headersOnly) {
    {
        std::lock_guard<std::mutex> lock(sync);
        if (finished) {
            return;
        }
        // record the range and let flushTo read it on the connection thread, off the dispatch hot path.
        fileMode = true;
        filePath = path;
        fileStart = start;
        fileLength = length;
        fileHeadersOnly = headersOnly;
        finished = true;
    }
    ready.notify_all();
}

void PocoDeferredResponse::writeHeaders(Poco::Net::HTTPServerResponse& response) {
    // clamp codes outside the valid http range before handing them to poco.
    if (statusCode < 100 || statusCode > 599) {
        statusCode = 500;
    }
    response.setStatusAndReason(static_cast<Poco::Net::HTTPResponse::HTTPStatus>(statusCode));

    bool hasContentType = false;
    for (const auto& [name, value] : headerMap) {
        response.set(name, value);
        if (Poco::icompare(name, "content-type") == 0) {
            hasContentType = true;
        }
    }

    // append multi-valued headers such as Set-Cookie without replacing earlier entries.
    for (const auto& [name, value] : extraHeaders) {
        response.add(name, value);
        if (Poco::icompare(name, "content-type") == 0) {
            hasContentType = true;
        }
    }

    if (!hasContentType) {
        response.setContentType("text/plain; charset=utf-8");
    }
}

void PocoDeferredResponse::streamFile(Poco::Net::HTTPServerResponse& response, const std::string& path,
                                      std::uint64_t start, std::uint64_t length, bool headersOnly) {
    if (headersOnly) {
        response.send();
        return;
    }

    std::ifstream file(path, std::ios::binary);
    std::ostream& out = response.send();
    if (!file) {
        return;
    }

    file.seekg(static_cast<std::streamoff>(start));

    // copy the range in bounded blocks so memory never scales with the file size.
    char buffer[65536];
    std::uint64_t remaining = length;
    while (remaining > 0) {
        const std::streamsize want = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, sizeof(buffer)));
        file.read(buffer, want);
        const std::streamsize got = file.gcount();
        if (got <= 0) {
            break;
        }
        out.write(buffer, got);
        remaining -= static_cast<std::uint64_t>(got);
    }
}

void PocoDeferredResponse::flushTo(Poco::Net::HTTPServerResponse& response) {
    std::unique_lock<std::mutex> lock(sync);

    constexpr auto pollInterval = std::chrono::milliseconds(200);
    const bool hasDeadline = requestTimeoutMs > 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(requestTimeoutMs);

    // wait until the handler produced output, finished, the server is stopping, or the deadline passed.
    while (chunks.empty() && !finished) {
        if (stopping && stopping->load(std::memory_order_acquire)) {
            lock.unlock();
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_SERVICE_UNAVAILABLE);
            response.setContentType("text/plain; charset=utf-8");
            response.send() << "Service unavailable.";
            return;
        }
        if (hasDeadline && std::chrono::steady_clock::now() >= deadline) {
            lock.unlock();
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_GATEWAY_TIMEOUT);
            response.setContentType("text/plain; charset=utf-8");
            response.send() << "Gateway timeout.";
            return;
        }
        ready.wait_for(lock, pollInterval);
    }

    // a file response reads its body here, on the connection thread, so the dispatcher never touches disk.
    if (fileMode) {
        writeHeaders(response);
        if (statusCode != 204 && statusCode != 304) {
            response.setContentLength64(static_cast<Poco::Int64>(fileLength));
        }
        const std::string path = filePath;
        const std::uint64_t start = fileStart;
        const std::uint64_t length = fileLength;
        const bool headersOnly = fileHeadersOnly;
        lock.unlock();
        streamFile(response, path, start, length, headersOnly);
        return;
    }

    if (streaming) {
        writeHeaders(response);
        response.setChunkedTransferEncoding(true);
        std::ostream& out = response.send();

        while (true) {
            while (!chunks.empty()) {
                const std::string chunk = std::move(chunks.front());
                chunks.pop_front();
                lock.unlock();
                out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                out.flush();
                lock.lock();
            }
            if (finished || (stopping && stopping->load(std::memory_order_acquire))) {
                break;
            }
            ready.wait_for(lock, pollInterval);
        }
        return;
    }

    writeHeaders(response);

    // 204 and 304 must not carry a content-length or a body.
    if (statusCode != 204 && statusCode != 304) {
        response.setContentLength64(static_cast<Poco::Int64>(payload.size()));
    }

    // release the lock before writing so a slow client cannot stall threads that check ended().
    const std::string body = std::move(payload);
    lock.unlock();

    std::ostream& out = response.send();
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
}

} // namespace varn::http
