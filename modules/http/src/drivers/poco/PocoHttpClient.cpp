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
#include <Poco/Net/Context.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/RejectCertificateHandler.h>
#include <Poco/Net/SSLManager.h>
#endif

namespace varn::http::client {

namespace {

std::string buildWire(int status, const std::string& body) {
    return "VARN/1 " + std::to_string(status) + " " + std::to_string(body.size()) + "\n" + body;
}

// a header injected with a control octet could split the request line, so reject it outright.
bool headerControlSafe(const std::string& value) {
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

void applyHeaders(Poco::Net::HTTPRequest& request, const std::map<std::string, std::string>& headers) {
    for (const auto& [name, value] : headers) {
        if (name.empty() || !headerControlSafe(name) || !headerControlSafe(value)) {
            throw std::runtime_error("[http] A request header name or value contains invalid characters.");
        }
        request.set(name, value);
    }
}

// reads the response body in bounded blocks so a hostile server cannot drive unbounded memory use.
std::string readResponseBody(std::istream& rs, std::size_t maxBytes) {
    std::string out;
    char buffer[65536];
    while (rs) {
        rs.read(buffer, sizeof(buffer));
        const std::streamsize got = rs.gcount();
        if (got <= 0) {
            break;
        }
        if (out.size() + static_cast<std::size_t>(got) > maxBytes) {
            throw std::runtime_error("[http] The response body exceeds the maximum allowed size.");
        }
        out.append(buffer, static_cast<std::size_t>(got));
    }
    return out;
}

std::string performHttp(
    const std::string& method,
    const Poco::URI& uri,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const ClientRequestOptions& options) {
    const std::string path = uri.getPathAndQuery().empty() ? "/" : uri.getPathAndQuery();
    const std::string host = uri.getHost();
    const Poco::UInt16 port = static_cast<Poco::UInt16>(
        uri.getPort() == 0 ? (uri.getScheme() == "https" ? 443 : 80) : uri.getPort());

    Poco::Net::HTTPClientSession session(host, port);
    session.setTimeout(Poco::Timespan(options.timeoutSeconds, 0));

    Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
    applyHeaders(request, headers);

    std::ostream& os = session.sendRequest(request);
    if (!body.empty()) {
        os << body;
    }

    Poco::Net::HTTPResponse response;
    std::istream& rs = session.receiveResponse(response);
    const int status = static_cast<int>(response.getStatus());
    return buildWire(status, readResponseBody(rs, options.maxResponseBytes));
}

#if defined(VARN_ENABLE_TLS)

// strict context validates the chain against the system CAs; the insecure one is an explicit opt-out.
Poco::Net::Context::Ptr tlsClientContext(bool verify) {
#if defined(_WIN32)
    if (verify) {
        static Poco::Net::Context::Ptr strict = new Poco::Net::Context(
            Poco::Net::Context::TLS_CLIENT_USE, "", Poco::Net::Context::VERIFY_STRICT,
            Poco::Net::Context::OPT_DEFAULTS, Poco::Net::Context::CERT_STORE_MY);
        return strict;
    }
    static Poco::Net::Context::Ptr insecure = new Poco::Net::Context(
        Poco::Net::Context::TLS_CLIENT_USE, "", Poco::Net::Context::VERIFY_NONE,
        Poco::Net::Context::OPT_DEFAULTS, Poco::Net::Context::CERT_STORE_MY);
    return insecure;
#else
    if (verify) {
        static Poco::Net::Context::Ptr strict = new Poco::Net::Context(
            Poco::Net::Context::TLS_CLIENT_USE, "", "", "", Poco::Net::Context::VERIFY_STRICT, 9, true,
            "DEFAULT@SECLEVEL=1");
        return strict;
    }
    static Poco::Net::Context::Ptr insecure = new Poco::Net::Context(
        Poco::Net::Context::TLS_CLIENT_USE, "", "", "", Poco::Net::Context::VERIFY_NONE, 9, false,
        "DEFAULT@SECLEVEL=1");
    return insecure;
#endif
}

void ensureTlsClientInitialized() {
    static Poco::Crypto::OpenSSLInitializer sslInitializer;
    static std::once_flag sslOnce;
    std::call_once(sslOnce, [] {
        // the default handler rejects invalid certificates so strict sessions actually fail closed.
        Poco::SharedPtr<Poco::Net::InvalidCertificateHandler> handler(new Poco::Net::RejectCertificateHandler(false));
        Poco::Net::SSLManager::instance().initializeClient(nullptr, handler, tlsClientContext(true));
    });
}

std::string performHttps(
    const std::string& method,
    const Poco::URI& uri,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const ClientRequestOptions& options) {
    ensureTlsClientInitialized();

    const std::string path = uri.getPathAndQuery().empty() ? "/" : uri.getPathAndQuery();
    const std::string host = uri.getHost();
    const Poco::UInt16 port = static_cast<Poco::UInt16>(uri.getPort() == 0 ? 443 : uri.getPort());

    Poco::Net::HTTPSClientSession session(host, port, tlsClientContext(options.verifyTls));
    session.setTimeout(Poco::Timespan(options.timeoutSeconds, 0));

    Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
    applyHeaders(request, headers);

    std::ostream& os = session.sendRequest(request);
    if (!body.empty()) {
        os << body;
    }

    Poco::Net::HTTPResponse response;
    std::istream& rs = session.receiveResponse(response);
    const int status = static_cast<int>(response.getStatus());
    return buildWire(status, readResponseBody(rs, options.maxResponseBytes));
}

#endif

} // namespace

std::string performRequestWire(
    const std::string& method,
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const ClientRequestOptions& options) {
    Poco::URI uri(url);
    const std::string scheme = uri.getScheme();
    if (scheme != "http" && scheme != "https") {
        throw std::runtime_error("[http] The URL scheme must be http or https.");
    }
    if (scheme == "https") {
#if defined(VARN_ENABLE_TLS)
        return performHttps(method, uri, headers, body, options);
#else
        throw std::runtime_error("[http] Secure URLs require a build with TLS support enabled.");
#endif
    }
    return performHttp(method, uri, headers, body, options);
}

} // namespace varn::http::client
