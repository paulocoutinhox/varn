#pragma once

#include "varn/http/HttpTypes.h"

#include <functional>
#include <memory>

namespace varn {

class Runtime;
using HttpHandler = std::function<void(const HttpRequest&, std::shared_ptr<HttpResponse>)>;

std::shared_ptr<HttpServer> createHttpServer(Runtime& runtime, HttpServerOptions options, HttpHandler handler);

} // namespace varn
