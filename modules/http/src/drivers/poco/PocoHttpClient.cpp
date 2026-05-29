#include "varn/http/HttpClientPerform.h"

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
#include <Poco/Timespan.h>
#include <Poco/URI.h>

#include <mutex>
#include <sstream>
#include <stdexcept>

#if defined(VARN_ENABLE_TLS)
#include <Poco/Crypto/OpenSSLInitializer.h>
#include <Poco/Net/AcceptCertificateHandler.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/SSLManager.h>
#endif

namespace varn::http::client {

namespace {

std::string buildWire(int status, const std::string& body) {
    return "VARN/1 " + std::to_string(status) + " " + std::to_string(body.size()) + "\n" + body;
}

void applyHeaders(Poco::Net::HTTPRequest& request, const std::map<std::string, std::string>& headers) {
    for (const auto& entry : headers) {
        request.set(entry.first, entry.second);
    }
}

std::string readResponseBody(std::istream& rs) {
    std::string out;
    Poco::StreamCopier::copyToString(rs, out);
    return out;
}

std::string performHttp(
    const std::string& method,
    const Poco::URI& uri,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    int timeoutSeconds) {
    const std::string path = uri.getPathAndQuery().empty() ? "/" : uri.getPathAndQuery();
    const std::string host = uri.getHost();
    const Poco::UInt16 port = static_cast<Poco::UInt16>(
        uri.getPort() == 0 ? (uri.getScheme() == "https" ? 443 : 80) : uri.getPort());

    Poco::Net::HTTPClientSession session(host, port);
    session.setTimeout(Poco::Timespan(timeoutSeconds, 0));

    Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
    applyHeaders(request, headers);

    std::ostream& os = session.sendRequest(request);
    if (!body.empty()) {
        os << body;
    }

    Poco::Net::HTTPResponse response;
    std::istream& rs = session.receiveResponse(response);
    const int status = static_cast<int>(response.getStatus());
    return buildWire(status, readResponseBody(rs));
}

#if defined(VARN_ENABLE_TLS)

Poco::Net::Context::Ptr tlsClientContext() {
#if defined(_WIN32)
    static Poco::Net::Context::Ptr context = new Poco::Net::Context(
        Poco::Net::Context::TLS_CLIENT_USE,
        "",
        Poco::Net::Context::VERIFY_RELAXED,
        Poco::Net::Context::OPT_DEFAULTS,
        Poco::Net::Context::CERT_STORE_MY);
#else
    static Poco::Net::Context::Ptr context = new Poco::Net::Context(
        Poco::Net::Context::TLS_CLIENT_USE,
        "",
        "",
        "",
        Poco::Net::Context::VERIFY_RELAXED,
        9,
        false,
        "DEFAULT@SECLEVEL=1");
#endif
    return context;
}

void ensureTlsClientInitialized() {
    static Poco::Crypto::OpenSSLInitializer sslInitializer;
    static std::once_flag sslOnce;
    std::call_once(sslOnce, [] {
        Poco::SharedPtr<Poco::Net::InvalidCertificateHandler> handler(new Poco::Net::AcceptCertificateHandler(false));
        Poco::Net::SSLManager::instance().initializeClient(nullptr, handler, tlsClientContext());
    });
}

std::string performHttps(
    const std::string& method,
    const Poco::URI& uri,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    int timeoutSeconds) {
    ensureTlsClientInitialized();

    const std::string path = uri.getPathAndQuery().empty() ? "/" : uri.getPathAndQuery();
    const std::string host = uri.getHost();
    const Poco::UInt16 port = static_cast<Poco::UInt16>(uri.getPort() == 0 ? 443 : uri.getPort());

    Poco::Net::HTTPSClientSession session(host, port, tlsClientContext());
    session.setTimeout(Poco::Timespan(timeoutSeconds, 0));

    Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
    applyHeaders(request, headers);

    std::ostream& os = session.sendRequest(request);
    if (!body.empty()) {
        os << body;
    }

    Poco::Net::HTTPResponse response;
    std::istream& rs = session.receiveResponse(response);
    const int status = static_cast<int>(response.getStatus());
    return buildWire(status, readResponseBody(rs));
}

#endif

} // namespace

std::string performRequestWire(
    const std::string& method,
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    int timeoutSeconds) {
    Poco::URI uri(url);
    const std::string scheme = uri.getScheme();
    if (scheme != "http" && scheme != "https") {
        throw std::runtime_error("[http] The URL scheme must be http or https.");
    }
    if (scheme == "https") {
#if defined(VARN_ENABLE_TLS)
        return performHttps(method, uri, headers, body, timeoutSeconds);
#else
        throw std::runtime_error("[http] Secure URLs require a build with TLS support enabled.");
#endif
    }
    return performHttp(method, uri, headers, body, timeoutSeconds);
}

} // namespace varn::http::client
