#include "varn/http/HttpServerFactory.h"

#if VARN_HTTP_SERVER_DRIVER_POCO
#include "varn/http/drivers/poco/PocoHttpServer.h"
#endif

#include <stdexcept>

namespace varn {

std::shared_ptr<HttpServer> createHttpServer(Runtime& runtime, HttpServerOptions options, HttpHandler handler) {
#if VARN_HTTP_SERVER_DRIVER_POCO
    return std::make_shared<PocoHttpServer>(runtime, std::move(options), std::move(handler));
#else
    (void)runtime;
    (void)options;
    (void)handler;
    throw std::runtime_error("HTTP server driver is not available: " VARN_HTTP_SERVER_DRIVER_NAME);
#endif
}

} // namespace varn
