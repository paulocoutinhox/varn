#if !defined(__EMSCRIPTEN__)
#error "EmscriptenFetchHttpClient is only built for Emscripten (VARN_HTTP_CLIENT_DRIVER=EMSCRIPTEN_FETCH)."
#endif

#include "varn/async/Promise.h"
#include "varn/http/HttpClientPerform.h"
#include "varn/wasm/WasmAsyncHost.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace varn::http::client {

using varn::async::Promise;

static std::string buildWire(int status, const std::string& body) {
    return "VARN/1 " + std::to_string(status) + " " + std::to_string(body.size()) + "\n" + body;
}

struct FetchContext {
    std::shared_ptr<Promise> promise;
    std::string method;
    std::string url;
    std::string body;
    std::string headersJson;
};

static void finishSuccess(FetchContext* ctx, int status, const void* data, size_t numBytes) {
    std::string respBody;
    if (data != nullptr && numBytes > 0) {
        respBody.assign(static_cast<const char*>(data), numBytes);
    }
    try {
        ctx->promise->resolve(buildWire(status, respBody));
    } catch (...) {
    }
    delete ctx;
}

static void finishError(FetchContext* ctx, const std::string& message) {
    try {
        ctx->promise->reject(message);
    } catch (...) {
    }
    delete ctx;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void varn_fetch_js_on_success(uintptr_t ctxHandle, int httpStatus, uintptr_t bodyPtr,
                                                   size_t bodyLen) {
    auto* ctx = reinterpret_cast<FetchContext*>(ctxHandle);
    const void* data = (bodyLen > 0 && bodyPtr != 0) ? reinterpret_cast<const void*>(bodyPtr) : nullptr;
    if (ctx != nullptr) {
        try {
            finishSuccess(ctx, httpStatus, data, bodyLen);
        } catch (...) {
        }
    }
    if (bodyPtr != 0) {
        std::free(reinterpret_cast<void*>(bodyPtr));
    }
    varn::wasm::httpClientFetchInflight().fetch_sub(1, std::memory_order_acq_rel);
}

EMSCRIPTEN_KEEPALIVE void varn_fetch_js_on_error(uintptr_t ctxHandle, uintptr_t msgPtr) {
    auto* ctx = reinterpret_cast<FetchContext*>(ctxHandle);
    std::string copy;
    if (msgPtr != 0) {
        copy.assign(reinterpret_cast<const char*>(msgPtr));
        std::free(reinterpret_cast<void*>(msgPtr));
    } else {
        copy = "http.client: fetch failed";
    }
    if (ctx != nullptr) {
        try {
            finishError(ctx, copy);
        } catch (...) {
        }
    }
    varn::wasm::httpClientFetchInflight().fetch_sub(1, std::memory_order_acq_rel);
}

} // extern "C"

EM_JS(void, varn_browser_fetch_start, (uintptr_t ctx, const char* url, const char* method, const char* headers_json,
                                         const char* body_ptr, int body_len, int timeout_sec), {
    function reportError(message) {
        const len = lengthBytesUTF8(message) + 1;
        const eptr = _malloc(len);
        if (!eptr) {
            _varn_fetch_js_on_error(ctx, 0);
            return;
        }
        stringToUTF8(message, eptr, len);
        _varn_fetch_js_on_error(ctx, eptr);
    }

    const urlStr = UTF8ToString(url);
    const methodStr = UTF8ToString(method);
    const headersStr = UTF8ToString(headers_json);

    let parsed;
    try {
        parsed = JSON.parse(headersStr);
    } catch (e) {
        reportError("http.client: invalid headers json (" + String(e) + ")");
        return;
    }
    if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) {
        reportError("http.client: headers json must be a JSON object");
        return;
    }
    const headersInit = {};
    for (const key of Object.keys(parsed)) {
        const value = parsed[key];
        if (typeof value !== "string") {
            reportError("http.client: header value must be a string (key: " + key + ")");
            return;
        }
        headersInit[key] = value;
    }

    let body = undefined;
    if (body_len > 0 && body_ptr) {
        body = HEAPU8.subarray(body_ptr, body_ptr + body_len);
    }

    const ctrl = new AbortController();
    let tid = 0;
    if (timeout_sec > 0) {
        tid = setTimeout(() => ctrl.abort(), timeout_sec * 1000);
    }

    fetch(urlStr, { method: methodStr, headers: headersInit, body: body, signal: ctrl.signal })
        .then((res) => {
            const st = res.status;
            return res.arrayBuffer().then((ab) => ({ status: st, ab: ab }));
        })
        .then((x) => {
            if (tid) {
                clearTimeout(tid);
            }
            const u8 = new Uint8Array(x.ab);
            const len = u8.byteLength;
            let ptr = 0;
            if (len > 0) {
                ptr = _malloc(len);
                if (!ptr) {
                    throw new Error("malloc failed for response body");
                }
                HEAPU8.set(u8, ptr);
            }
            _varn_fetch_js_on_success(ctx, x.status, ptr, len);
        })
        .catch((e) => {
            if (tid) {
                clearTimeout(tid);
            }
            reportError(String(e));
        });
});

void performRequestWireEmscriptenAsync(
    const std::shared_ptr<Promise>& promise,
    const std::string& method,
    const std::string& url,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    int timeoutSeconds) {
    auto* ctx = new FetchContext{promise, method, url, body, {}};
    nlohmann::json hdr = nlohmann::json::object();
    for (const auto& kv : headers) {
        hdr[kv.first] = kv.second;
    }
    ctx->headersJson = hdr.dump();

    varn::wasm::httpClientFetchInflight().fetch_add(1, std::memory_order_relaxed);

    varn_browser_fetch_start(reinterpret_cast<uintptr_t>(ctx), ctx->url.c_str(), ctx->method.c_str(),
                               ctx->headersJson.c_str(), ctx->body.empty() ? nullptr : ctx->body.data(),
                               static_cast<int>(ctx->body.size()), timeoutSeconds);
}

} // namespace varn::http::client
