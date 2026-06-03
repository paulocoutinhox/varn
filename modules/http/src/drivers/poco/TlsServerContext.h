#pragma once

#ifdef VARN_ENABLE_TLS

#include "varn/http/HttpTypes.h"

#include <Poco/Net/Context.h>

#include <string>
#include <string_view>

namespace varn::http {

class TlsServerContext {
public:
    static Poco::Net::Context::Ptr create(const HttpServerOptions& opts);
    static void initializeSslManager(Poco::Net::Context::Ptr context);

private:
    static void requireKeyMaterial(const HttpServerOptions& opts);

#if defined(_WIN32)
    static bool endsWithIgnoreCase(std::string_view value, std::string_view suffix);
    static bool pathLooksLikePkcs12(const std::string& path);
#endif
};

} // namespace varn::http

#endif
