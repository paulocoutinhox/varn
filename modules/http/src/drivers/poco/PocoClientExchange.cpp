#include "PocoClientExchange.h"

#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Timespan.h>

#include <ostream>
#include <stdexcept>
#include <string>

#if defined(VARN_ENABLE_TLS)
#include <Poco/Crypto/OpenSSLInitializer.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/PrivateKeyPassphraseHandler.h>
#include <Poco/Net/RejectCertificateHandler.h>
#include <Poco/Net/SSLManager.h>

#include <mutex>
#endif

namespace varn::http::client
{

std::string PocoClientExchange::performHttp(
    const std::string& method,
    const Poco::URI& uri,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const ClientRequestOptions& options)
{
    const std::string path = uri.getPathAndQuery().empty() ? "/" : uri.getPathAndQuery();
    const std::string host = uri.getHost();
    const Poco::UInt16 port = static_cast<Poco::UInt16>(uri.getPort());

    Poco::Net::HTTPClientSession session(host, port);
    session.setTimeout(Poco::Timespan(options.timeoutSeconds, 0));

    Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
    applyHeaders(request, headers);

    std::ostream& os = session.sendRequest(request);
    if (!body.empty())
    {
        os << body;
    }

    Poco::Net::HTTPResponse response;
    std::istream& rs = session.receiveResponse(response);
    const int status = static_cast<int>(response.getStatus());
    return buildWire(status, readResponseBody(rs, options.maxResponseBytes));
}

#if defined(VARN_ENABLE_TLS)
std::string PocoClientExchange::performHttps(
    const std::string& method,
    const Poco::URI& uri,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    const ClientRequestOptions& options)
{
    ensureTlsClientInitialized();

    const std::string path = uri.getPathAndQuery().empty() ? "/" : uri.getPathAndQuery();
    const std::string host = uri.getHost();
    const Poco::UInt16 port = static_cast<Poco::UInt16>(uri.getPort());

    Poco::Net::HTTPSClientSession session(host, port, tlsClientContext(options.verifyTls));
    session.setTimeout(Poco::Timespan(options.timeoutSeconds, 0));

    Poco::Net::HTTPRequest request(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
    applyHeaders(request, headers);

    std::ostream& os = session.sendRequest(request);
    if (!body.empty())
    {
        os << body;
    }

    Poco::Net::HTTPResponse response;
    std::istream& rs = session.receiveResponse(response);
    const int status = static_cast<int>(response.getStatus());
    return buildWire(status, readResponseBody(rs, options.maxResponseBytes));
}
#endif

std::string PocoClientExchange::buildWire(int status, const std::string& body)
{
    return "VARN/1 " + std::to_string(status) + " " + std::to_string(body.size()) + "\n" + body;
}

bool PocoClientExchange::headerControlSafe(const std::string& value)
{
    for (unsigned char c : value)
    {
        if (c < 0x20 || c == 0x7f)
        {
            return false;
        }
    }

    return true;
}

void PocoClientExchange::applyHeaders(Poco::Net::HTTPRequest& request, const std::map<std::string, std::string>& headers)
{
    for (const auto& [name, value] : headers)
    {
        if (name.empty() || !headerControlSafe(name) || !headerControlSafe(value))
        {
            throw std::runtime_error("[PocoClientExchange] A request header name or value contains invalid characters.");
        }

        request.set(name, value);
    }
}

std::string PocoClientExchange::readResponseBody(std::istream& rs, std::size_t maxBytes)
{
    std::string out;
    char buffer[65536];
    while (rs)
    {
        rs.read(buffer, sizeof(buffer));
        const std::streamsize got = rs.gcount();
        if (got <= 0)
        {
            break;
        }

        if (out.size() + static_cast<std::size_t>(got) > maxBytes)
        {
            throw std::runtime_error("[PocoClientExchange] The response body exceeds the maximum allowed size.");
        }

        out.append(buffer, static_cast<std::size_t>(got));
    }

    return out;
}

#if defined(VARN_ENABLE_TLS)
Poco::Net::Context::Ptr PocoClientExchange::tlsClientContext(bool verify)
{
#if defined(_WIN32)
    if (verify)
    {
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
    if (verify)
    {
        static Poco::Net::Context::Ptr strict = new Poco::Net::Context(
            Poco::Net::Context::TLS_CLIENT_USE, "", "", "", Poco::Net::Context::VERIFY_STRICT, 9, true,
            "DEFAULT@SECLEVEL=2");
        return strict;
    }

    static Poco::Net::Context::Ptr insecure = new Poco::Net::Context(
        Poco::Net::Context::TLS_CLIENT_USE, "", "", "", Poco::Net::Context::VERIFY_NONE, 9, false,
        "DEFAULT@SECLEVEL=2");
    return insecure;
#endif
}

void PocoClientExchange::ensureTlsClientInitialized()
{
    static Poco::Crypto::OpenSSLInitializer sslInitializer;
    static std::once_flag sslOnce;
    std::call_once(sslOnce, []
                   {
        // the default handler rejects invalid certificates so strict sessions actually fail closed.
        Poco::SharedPtr<Poco::Net::InvalidCertificateHandler> handler(new Poco::Net::RejectCertificateHandler(false));
        Poco::Net::SSLManager::instance().initializeClient(nullptr, handler, tlsClientContext(true)); });
}
#endif

} // namespace varn::http::client
