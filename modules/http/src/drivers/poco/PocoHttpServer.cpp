#include "varn/log/Log.h"
#include "varn/http/drivers/poco/PocoHttpServer.h"
#include "varn/runtime/Runtime.h"

#include <Poco/Net/HTMLForm.h>
#include <Poco/Net/HTTPCookie.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerParams.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Net/MediaType.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/URI.h>

#ifdef VARN_ENABLE_TLS
#include <Poco/Crypto/OpenSSLInitializer.h>
#include <Poco/Net/AcceptCertificateHandler.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/KeyFileHandler.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/Net/SecureServerSocket.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace varn::http {

using varn::runtime::Runtime;

namespace {

std::map<std::string, std::string> extractHeaders(const Poco::Net::HTTPRequest& request) {
    std::map<std::string, std::string> headers;

    for (auto it = request.begin(); it != request.end(); ++it) {
        headers[it->first] = it->second;
    }

    return headers;
}

std::map<std::string, std::string> extractQuery(const Poco::URI& uri) {
    std::map<std::string, std::string> query;
    Poco::URI::QueryParameters params = uri.getQueryParameters();

    for (const auto& param : params) {
        query[param.first] = param.second;
    }

    return query;
}

std::map<std::string, std::string> extractCookies(const Poco::Net::HTTPServerRequest& request) {
    std::map<std::string, std::string> cookies;
    Poco::Net::NameValueCollection values;

    try {
        request.getCookies(values);
        for (auto it = values.begin(); it != values.end(); ++it) {
            cookies[it->first] = it->second;
        }
    } catch (const std::exception& ex) {
        log::Log::line("extractCookies", "getCookies", std::string("Cookie header rejected: ") + ex.what());
    }

    return cookies;
}

std::string readBody(Poco::Net::HTTPServerRequest& request, int maxBytes) {
    std::istream& input = request.stream();
    std::ostringstream body;
    char buffer[8192];
    int total = 0;

    while (input.good()) {
        input.read(buffer, sizeof(buffer));
        std::streamsize count = input.gcount();
        if (count <= 0) {
            break;
        }

        total += static_cast<int>(count);
        if (total > maxBytes) {
            throw std::runtime_error("request body too large");
        }

        body.write(buffer, count);
    }

    return body.str();
}

HttpRequest convertRequest(Poco::Net::HTTPServerRequest& request, const HttpServerOptions& opts) {
    Poco::URI uri(request.getURI());

    HttpRequest out;
    out.host = request.getHost();
    out.method = request.getMethod();
    out.target = request.getURI();
    out.path = uri.getPath().empty() ? "/" : uri.getPath();
    out.queryString = uri.getRawQuery();
    out.headers = extractHeaders(request);
    out.cookies = extractCookies(request);
    out.query = extractQuery(uri);
    out.remoteAddress = request.clientAddress().toString();
    out.body = readBody(request, opts.maxRequestBodyBytes);

    return out;
}

class VarnRequestHandler final : public Poco::Net::HTTPRequestHandler {
public:
    VarnRequestHandler(HttpServerOptions opts, HttpHandler onRequest)
        : httpOptions(std::move(opts)), onRequest(std::move(onRequest)), publicFiles(httpOptions.publicDir) {}

    void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override {
        try {
            auto deferredResponse = std::make_shared<PocoDeferredResponse>();
            HttpRequest inboundRequest = convertRequest(request, httpOptions);

            if (httpOptions.servePublic && publicFiles.tryServe(inboundRequest, *deferredResponse)) {
                deferredResponse->flushTo(response);
                return;
            }

            onRequest(inboundRequest, deferredResponse);

            // write the buffered response after the handler returns when the library still owns the stream
            deferredResponse->flushTo(response);
        } catch (const std::exception& ex) {
            std::string detail = std::string("Unexpected exception: ") + ex.what();
            log::Log::error("VarnRequestHandler", "handleRequest", detail);
            response.setStatus(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
            response.setContentType("text/plain; charset=utf-8");
            response.send() << "Internal server error: " << ex.what();
        }
    }

private:
    HttpServerOptions httpOptions;
    HttpHandler onRequest;
    StaticFileHandler publicFiles;
};

class VarnRequestHandlerFactory final : public Poco::Net::HTTPRequestHandlerFactory {
public:
    VarnRequestHandlerFactory(HttpServerOptions opts, HttpHandler onRequest)
        : httpOptions(std::move(opts)), onRequest(std::move(onRequest)) {}

    Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest&) override {
        return new VarnRequestHandler(httpOptions, onRequest);
    }

private:
    HttpServerOptions httpOptions;
    HttpHandler onRequest;
};

#ifdef VARN_ENABLE_TLS
namespace {

bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(value[value.size() - suffix.size() + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

bool pathLooksLikePkcs12(const std::string& path) {
    return endsWithIgnoreCase(path, ".pfx") || endsWithIgnoreCase(path, ".p12");
}

} // namespace

void requireTlsKeyMaterialExists(const HttpServerOptions& opts) {
    if (opts.keyFile.empty() || opts.certFile.empty()) {
        throw std::runtime_error("tls is enabled but keyFile or certFile is empty");
    }
    for (const auto& path : {opts.keyFile, opts.certFile}) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("tls file not found: " + path);
        }
    }
}

Poco::Net::Context::Ptr createTlsServerContext(const HttpServerOptions& opts) {
    requireTlsKeyMaterialExists(opts);

    static Poco::Crypto::OpenSSLInitializer openSslInitializer;

#if defined(_WIN32)
    // windows tls uses one pkcs12 bundle instead of separate pem key and certificate paths
    std::string pkcs12Path;
    if (pathLooksLikePkcs12(opts.certFile)) {
        pkcs12Path = opts.certFile;
    } else if (pathLooksLikePkcs12(opts.keyFile)) {
        pkcs12Path = opts.keyFile;
    } else {
        throw std::runtime_error(
            "Windows HTTPS uses Schannel (Poco NetSSL_Win): set certFile or keyFile to a .pfx/.p12 bundle "
            "(e.g. openssl pkcs12 -export -inkey key.pem -in cert.pem -out server.pfx). "
            "Separate PEM key + cert paths are not supported on Windows.");
    }
    const int winOptions = Poco::Net::Context::OPT_DEFAULTS | Poco::Net::Context::OPT_LOAD_CERT_FROM_FILE;
    return new Poco::Net::Context(
        Poco::Net::Context::TLS_SERVER_USE,
        pkcs12Path,
        Poco::Net::Context::VERIFY_NONE,
        winOptions,
        Poco::Net::Context::CERT_STORE_MY);
#else
    return new Poco::Net::Context(
        Poco::Net::Context::TLS_SERVER_USE,
        opts.keyFile,
        opts.certFile,
        "",
        Poco::Net::Context::VERIFY_NONE,
        9,
        true,
        "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");
#endif
}

void initializeSslManagerForServer(Poco::Net::Context::Ptr context) {
    auto privateKeyHandler = Poco::SharedPtr<Poco::Net::PrivateKeyPassphraseHandler>(
        new Poco::Net::KeyFileHandler(false)
    );

    auto invalidCertHandler = Poco::SharedPtr<Poco::Net::InvalidCertificateHandler>(
        new Poco::Net::AcceptCertificateHandler(false)
    );

    Poco::Net::SSLManager::instance().initializeServer(privateKeyHandler, invalidCertHandler, context);
}
#endif

} // namespace

PocoHttpServer::PocoHttpServer(Runtime& rt, HttpServerOptions opts, HttpHandler onRequest)
    : runtime(rt), serverOptions(std::move(opts)), httpHandler(std::move(onRequest)) {}

PocoHttpServer::~PocoHttpServer() {
    stop();
}

void PocoHttpServer::start() {
    auto params = Poco::Net::HTTPServerParams::Ptr(new Poco::Net::HTTPServerParams());
    params->setMaxQueued(serverOptions.maxQueued);
    if (serverOptions.maxThreads > 0) {
        params->setMaxThreads(serverOptions.maxThreads);
    }
    params->setKeepAlive(true);
    params->setKeepAliveTimeout(Poco::Timespan(serverOptions.keepAliveTimeoutSeconds, 0));

    auto factory = Poco::Net::HTTPRequestHandlerFactory::Ptr(
        new VarnRequestHandlerFactory(serverOptions, httpHandler)
    );

    const Poco::Net::SocketAddress socketAddress(serverOptions.host, static_cast<Poco::UInt16>(serverOptions.port));
    const int listenBacklog = std::clamp(serverOptions.maxQueued, 1, 65535);

#ifdef VARN_ENABLE_TLS
    if (serverOptions.tls) {
        Poco::Net::Context::Ptr tlsContext = createTlsServerContext(serverOptions);
        initializeSslManagerForServer(tlsContext);

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
        server->stop();
        server.reset();
    }
}

void PocoDeferredResponse::setStatus(int code) {
    std::lock_guard<std::mutex> lock(sync);
    statusCode = code;
}

void PocoDeferredResponse::setHeader(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(sync);
    headerMap[name] = value;
}

void PocoDeferredResponse::end(const std::string& body) {
    {
        std::lock_guard<std::mutex> lock(sync);
        if (finished) {
            return;
        }
        payload = body;
        finished = true;
    }
    ready.notify_all();
}

bool PocoDeferredResponse::ended() const {
    std::lock_guard<std::mutex> lock(sync);
    return finished;
}

void PocoDeferredResponse::flushTo(Poco::Net::HTTPServerResponse& response) {
    std::unique_lock<std::mutex> lock(sync);
    ready.wait(lock, [&] { return finished; });

    response.setStatus(static_cast<Poco::Net::HTTPResponse::HTTPStatus>(statusCode));

    bool hasContentType = false;
    for (const auto& [name, value] : headerMap) {
        response.set(name, value);
        if (Poco::icompare(name, "content-type") == 0) {
            hasContentType = true;
        }
    }

    if (!hasContentType) {
        response.setContentType("text/plain; charset=utf-8");
    }

    response.setContentLength64(static_cast<Poco::Int64>(payload.size()));
    std::ostream& out = response.send();
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
}

} // namespace varn::http
