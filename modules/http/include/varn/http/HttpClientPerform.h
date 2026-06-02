#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>

namespace varn::async {
class Promise;
}

namespace varn::http::client {

struct ClientRequestOptions {
    int timeoutSeconds = 60;
    bool verifyTls = true;
    std::size_t maxResponseBytes = 64u * 1024u * 1024u;
};

#if defined(__EMSCRIPTEN__) && defined(VARN_HTTP_CLIENT_DRIVER_EMSCRIPTEN_FETCH) && VARN_HTTP_CLIENT_DRIVER_EMSCRIPTEN_FETCH
#define VARN_HTTP_CLIENT_EMSCRIPTEN_FETCH_ASYNC 1
#else
#define VARN_HTTP_CLIENT_EMSCRIPTEN_FETCH_ASYNC 0
#endif

#if VARN_HTTP_CLIENT_EMSCRIPTEN_FETCH_ASYNC
void performRequestWireEmscriptenAsync(
    const std::shared_ptr<varn::async::Promise>& promise,
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
    const ClientRequestOptions& options);
#endif

} // namespace varn::http::client
