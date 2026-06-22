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

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
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
        static const std::string caBundle = resolveCaBundle();
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

void driveHandshake(EventLoop& loop, std::shared_ptr<Poco::Net::SecureStreamSocket> socket,
                    std::shared_ptr<bool> settled, std::shared_ptr<ConnectCallback> shared);

void watchHandshake(EventLoop& loop, std::shared_ptr<Poco::Net::SecureStreamSocket> socket, bool onWrite,
                    std::shared_ptr<bool> settled, std::shared_ptr<ConnectCallback> shared)
{
    auto handler = [&loop, socket, settled, shared]() -> bool
    {
        if (*settled)
        {
            return true;
        }

        driveHandshake(loop, socket, settled, shared);
        return true;
    };

    if (onWrite)
    {
        loop.watchWrite(*socket, handler);
        return;
    }

    loop.watchRead(*socket, handler);
}

void driveHandshake(EventLoop& loop, std::shared_ptr<Poco::Net::SecureStreamSocket> socket,
                    std::shared_ptr<bool> settled, std::shared_ptr<ConnectCallback> shared)
{
    int result = 0;
    try
    {
        result = socket->completeHandshake();
    }
    catch (const std::exception& ex)
    {
        *settled = true;
        loop.closeSocket(*socket);
        (*shared)(nullptr, ex.what());
        return;
    }

    // the handshake may need another readable or writable turn before it settles
    if (result == Poco::Net::SecureStreamSocket::ERR_SSL_WANT_READ)
    {
        watchHandshake(loop, socket, false, settled, shared);
        return;
    }

    if (result == Poco::Net::SecureStreamSocket::ERR_SSL_WANT_WRITE)
    {
        watchHandshake(loop, socket, true, settled, shared);
        return;
    }

    *settled = true;
    (*shared)(std::make_shared<PocoStreamConnection>(*socket, loop), "");
}

} // namespace

void SocketTransport::connectTlsAsync(varn::runtime::Runtime& runtime, const std::string& host, int port,
                                      int timeoutMs, bool verify, ConnectCallback callback)
{
    EventLoop& loop = runtime.mainLoop();

    std::shared_ptr<Poco::Net::SecureStreamSocket> secure;
    try
    {
        secure = std::make_shared<Poco::Net::SecureStreamSocket>(tlsClientContext(verify));
        secure->setPeerHostName(host);
        secure->setLazyHandshake(true);
        secure->connectNB(Poco::Net::SocketAddress(host, static_cast<Poco::UInt16>(port)));
    }
    catch (const std::exception& ex)
    {
        callback(nullptr, ex.what());
        return;
    }

    secure->setBlocking(false);

    // the connect watcher and the optional timeout race to settle and the first to fire wins
    auto settled = std::make_shared<bool>(false);
    auto shared = std::make_shared<ConnectCallback>(std::move(callback));

    loop.watchWrite(*secure, [&loop, secure, settled, shared]() -> bool
                    {
        if (*settled)
        {
            return true;
        }

        int error = 0;
        try
        {
            error = secure->impl()->socketError();
        }
        catch (...)
        {
            error = -1;
        }

        if (error != 0)
        {
            *settled = true;
            (*shared)(nullptr, "[SocketTransport] The connection could not be established.");
            return true;
        }

        // starts the tls handshake on the same fd once the transport is connected
        driveHandshake(loop, secure, settled, shared);
        return true; });

    if (timeoutMs > 0)
    {
        loop.postDelayed(timeoutMs, [&loop, secure, settled, shared]()
                         {
            if (*settled)
            {
                return;
            }

            *settled = true;
            loop.closeSocket(*secure);
            (*shared)(nullptr, "[SocketTransport] The connection timed out."); });
    }
}

} // namespace varn::socket
