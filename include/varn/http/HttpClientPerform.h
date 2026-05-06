#pragma once

#include <map>
#include <memory>
#include <string>

namespace varn {

class Promise;

namespace http::client {

#if defined(__EMSCRIPTEN__) && defined(VARN_HTTP_CLIENT_DRIVER_EMSCRIPTEN_FETCH) && VARN_HTTP_CLIENT_DRIVER_EMSCRIPTEN_FETCH
#define VARN_HTTP_CLIENT_EMSCRIPTEN_FETCH_ASYNC 1
#else
#define VARN_HTTP_CLIENT_EMSCRIPTEN_FETCH_ASYNC 0
#endif

#if VARN_HTTP_CLIENT_EMSCRIPTEN_FETCH_ASYNC
void performRequestWireEmscriptenAsync(
    const std::shared_ptr<Promise>& promise,
    const std::string& method,
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    int timeoutSeconds);
#else
std::string performRequestWire(
    const std::string& method,
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    int timeoutSeconds);
#endif

} // namespace http::client

} // namespace varn
