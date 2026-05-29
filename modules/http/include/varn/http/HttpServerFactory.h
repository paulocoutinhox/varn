#pragma once

#include "varn/http/HttpTypes.h"

#include <functional>
#include <memory>

namespace varn::runtime {
class Runtime;
}

namespace varn::http {

using HttpHandler = std::function<void(const HttpRequest&, std::shared_ptr<HttpResponse>)>;

std::shared_ptr<HttpServer> createHttpServer(varn::runtime::Runtime& runtime, HttpServerOptions options, HttpHandler handler);

} // namespace varn::http
