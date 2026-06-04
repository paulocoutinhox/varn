#include "TlsServerContext.h"

#ifdef VARN_ENABLE_TLS

#include <Poco/Crypto/OpenSSLInitializer.h>
#include <Poco/Net/AcceptCertificateHandler.h>
#include <Poco/Net/KeyFileHandler.h>
#include <Poco/Net/PrivateKeyPassphraseHandler.h>
#include <Poco/Net/SSLManager.h>

#include <cctype>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace varn::http {

Poco::Net::Context::Ptr TlsServerContext::create(const HttpServerOptions& opts) {
    requireKeyMaterial(opts);

    static Poco::Crypto::OpenSSLInitializer openSslInitializer;

    // a modern suite of forward-secret aead ciphers, leaving tls 1.3 to negotiate its own.
    constexpr const char* kCipherList =
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
        "DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384";

#if defined(_WIN32)
    // windows tls uses one pkcs12 bundle instead of separate pem key and certificate paths
    std::string pkcs12Path;
    if (pathLooksLikePkcs12(opts.certFile)) {
        pkcs12Path = opts.certFile;
    } else if (pathLooksLikePkcs12(opts.keyFile)) {
        pkcs12Path = opts.keyFile;
    } else {
        throw std::runtime_error("[TlsServerContext] On Windows the TLS certificate must be a single bundle file.");
    }
    const int winOptions = Poco::Net::Context::OPT_DEFAULTS | Poco::Net::Context::OPT_LOAD_CERT_FROM_FILE;
    Poco::Net::Context::Ptr context = new Poco::Net::Context(
        Poco::Net::Context::TLS_SERVER_USE,
        pkcs12Path,
        Poco::Net::Context::VERIFY_NONE,
        winOptions,
        Poco::Net::Context::CERT_STORE_MY);
#else
    Poco::Net::Context::Ptr context = new Poco::Net::Context(
        Poco::Net::Context::TLS_SERVER_USE,
        opts.keyFile,
        opts.certFile,
        "",
        Poco::Net::Context::VERIFY_NONE,
        9,
        true,
        kCipherList);
#endif

    // refuse the legacy protocols that modern deployments must not negotiate.
    context->requireMinimumProtocol(Poco::Net::Context::PROTO_TLSV1_2);
    return context;
}

void TlsServerContext::initializeSslManager(Poco::Net::Context::Ptr context) {
    // the ssl manager is a process-global singleton; initialize its default handlers once so a second
    // server start does not clobber the first server's configuration. each server still binds its own
    // context to its socket, so the global default only supplies the passphrase/invalid-cert handlers.
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [&context] {
        auto privateKeyHandler = Poco::SharedPtr<Poco::Net::PrivateKeyPassphraseHandler>(
            new Poco::Net::KeyFileHandler(false)
        );

        auto invalidCertHandler = Poco::SharedPtr<Poco::Net::InvalidCertificateHandler>(
            new Poco::Net::AcceptCertificateHandler(false)
        );

        Poco::Net::SSLManager::instance().initializeServer(privateKeyHandler, invalidCertHandler, context);
    });
}

void TlsServerContext::requireKeyMaterial(const HttpServerOptions& opts) {
    if (opts.keyFile.empty() || opts.certFile.empty()) {
        throw std::runtime_error("[TlsServerContext] TLS is enabled but the key or certificate path is empty.");
    }
    for (const auto& path : {opts.keyFile, opts.certFile}) {
        if (!std::filesystem::exists(path)) {
            throw std::runtime_error("[TlsServerContext] A TLS key or certificate file was not found.");
        }
    }
}

#if defined(_WIN32)
bool TlsServerContext::endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
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

bool TlsServerContext::pathLooksLikePkcs12(const std::string& path) {
    return endsWithIgnoreCase(path, ".pfx") || endsWithIgnoreCase(path, ".p12");
}
#endif

} // namespace varn::http

#endif
