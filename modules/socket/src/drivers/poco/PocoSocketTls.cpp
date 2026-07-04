#include "varn/socket/SocketTransport.h"

#include "PocoSocketStream.h"

#include "varn/runtime/EventLoop.h"
#include "varn/runtime/Runtime.h"

#include <Poco/Crypto/OpenSSLInitializer.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/InvalidCertificateHandler.h>
#include <Poco/Net/PrivateKeyPassphraseHandler.h>
#include <Poco/Net/RejectCertificateHandler.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/Net/SecureStreamSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/StreamSocket.h>
#include <Poco/Timespan.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace varn::socket
{

using varn::runtime::EventLoop;

namespace
{

#if !defined(_WIN32)
// the bundled openssl ships no trust store, so point verification at the os ca bundle
std::string resolveCaBundle()
{
    if (const char* env = std::getenv("SSL_CERT_FILE"); env != nullptr && env[0] != '\0')
    {
        return env;
    }

    static const char* const candidates[] = {
        "/etc/ssl/cert.pem",
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        "/opt/homebrew/etc/openssl@3/cert.pem",
        "/usr/local/etc/openssl@3/cert.pem",
    };

    for (const char* path : candidates)
    {
        std::error_code ec;
        if (std::filesystem::exists(path, ec))
        {
            return path;
        }
    }

    return std::string();
}
#endif

Poco::Net::Context::Ptr tlsClientContext(bool verify)
{
    static Poco::Crypto::OpenSSLInitializer openSslInitializer;

    // sets the ssl manager default handler once so a strict session fails closed on an invalid certificate
    static std::once_flag onceFlag;
    std::call_once(onceFlag, []
                   {
        Poco::SharedPtr<Poco::Net::InvalidCertificateHandler> handler(new Poco::Net::RejectCertificateHandler(false));
        Poco::Net::SSLManager::instance().initializeClient(nullptr, handler, nullptr); });

#if defined(_WIN32)
    if (verify)
    {
        // verify against the system root store since the personal store holds no trusted roots
        static Poco::Net::Context::Ptr strict = new Poco::Net::Context(
            Poco::Net::Context::TLS_CLIENT_USE, "", Poco::Net::Context::VERIFY_STRICT,
            Poco::Net::Context::OPT_DEFAULTS, Poco::Net::Context::CERT_STORE_ROOT);
        return strict;
    }

    static Poco::Net::Context::Ptr insecure = new Poco::Net::Context(
        Poco::Net::Context::TLS_CLIENT_USE, "", Poco::Net::Context::VERIFY_NONE,
        Poco::Net::Context::OPT_DEFAULTS, Poco::Net::Context::CERT_STORE_ROOT);
    return insecure;
#else
    if (verify)
    {
        static const std::string caBundle = resolveCaBundle();
        if (caBundle.empty())
        {
            throw std::runtime_error("[SocketTls] No system CA trust store was found for certificate verification.");
        }

        static Poco::Net::Context::Ptr strict = new Poco::Net::Context(
            Poco::Net::Context::TLS_CLIENT_USE, "", "", caBundle, Poco::Net::Context::VERIFY_STRICT, 9, true,
            "DEFAULT@SECLEVEL=2");
        return strict;
    }

    static Poco::Net::Context::Ptr insecure = new Poco::Net::Context(
        Poco::Net::Context::TLS_CLIENT_USE, "", "", "", Poco::Net::Context::VERIFY_NONE, 9, false,
        "DEFAULT@SECLEVEL=2");
    return insecure;
#endif
}

} // namespace

void PocoStreamConnection::startTlsAsync(varn::runtime::Runtime& runtime, std::string host, bool verify, SendCallback callback)
{
    if (closed)
    {
        callback(false, "[PocoStreamConnection] The connection is closed.");
        return;
    }

    auto self = std::static_pointer_cast<PocoStreamConnection>(shared_from_this());
    varn::runtime::Runtime* rt = &runtime;
    varn::runtime::EventLoop* loop = &runtime.mainLoop();
    auto done = std::make_shared<SendCallback>(std::move(callback));

    // run the whole tls handshake blocking on the io pool so the ssl backend drives it synchronously, which avoids the readiness races the loop-driven handshake hits on windows schannel
    // clang-format off
    runtime.ioPool().post([self, rt, loop, host, verify, done]
    {
        bool ok = false;
        std::string error;
        Poco::Net::StreamSocket upgraded;
        try
        {
            self->socket.setBlocking(true);
            Poco::Net::SecureStreamSocket secure = Poco::Net::SecureStreamSocket::attach(self->socket, host, tlsClientContext(verify));
            secure.setBlocking(true);
            secure.completeHandshake();
            upgraded = secure;
            ok = true;
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }

        // hand the settled transport back to the loop thread that owns the connection
        loop->post([self, rt, upgraded, ok, error, done]() mutable
        {
            if (ok)
            {
                self->adoptSecure(upgraded, *rt);
                (*done)(true, std::string());
                return;
            }

            (*done)(false, error);
        });
    });
    // clang-format on
}

void SocketTransport::connectTlsAsync(varn::runtime::Runtime& runtime, const std::string& host, int port,
                                      int timeoutMs, bool verify, ConnectCallback callback)
{
    varn::runtime::Runtime* rt = &runtime;
    EventLoop* loop = &runtime.mainLoop();
    auto shared = std::make_shared<ConnectCallback>(std::move(callback));

    // connect and handshake blocking on the io pool so the ssl backend drives the whole exchange synchronously, keeping schannel off the loop readiness poll
    // clang-format off
    runtime.ioPool().post([rt, loop, host, port, timeoutMs, verify, shared]
    {
        std::shared_ptr<PocoStreamConnection> connection;
        std::string error;
        try
        {
            // connect the plaintext transport first, then upgrade it exactly like the mid-stream startTls path since the schannel backend drives an attached handshake reliably where its own connect path does not
            Poco::Net::StreamSocket raw;
            Poco::Net::SocketAddress address(host, static_cast<Poco::UInt16>(port));
            if (timeoutMs > 0)
            {
                raw.connect(address, Poco::Timespan(static_cast<Poco::Timespan::TimeDiff>(timeoutMs) * 1000));
            }
            else
            {
                raw.connect(address);
            }

            Poco::Net::SecureStreamSocket secure = Poco::Net::SecureStreamSocket::attach(raw, host, tlsClientContext(verify));
            secure.setBlocking(true);
            secure.completeHandshake();
            connection = std::make_shared<PocoStreamConnection>(secure, *loop);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }

        loop->post([rt, connection, error, shared]() mutable
        {
            if (connection)
            {
                connection->markSecure(*rt);
                (*shared)(connection, std::string());
                return;
            }

            (*shared)(nullptr, error);
        });
    });
    // clang-format on
}

} // namespace varn::socket
