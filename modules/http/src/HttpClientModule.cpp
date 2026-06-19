#include "varn/http/HttpClientModule.h"

#include "varn/http/HttpClientPerform.h"
#include "varn/async/Promise.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"

#include <lua.hpp>

#include <map>
#include <memory>
#include <string>

namespace varn::http {

using varn::runtime::Runtime;
using varn::async::Promise;

Runtime& HttpClientModule::luaRuntime(lua_State* L) {
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

void HttpClientModule::readHeadersTable(lua_State* L, int absIndex, std::map<std::string, std::string>& out) {
    lua_pushnil(L);
    while (lua_next(L, absIndex) != 0) {
        // coerce a copy of the key so lua_next still sees the original on the next step.
        lua_pushvalue(L, -2);
        const char* key = lua_tostring(L, -1);
        const char* val = lua_tostring(L, -2);
        if (key != nullptr && val != nullptr) {
            out.emplace(key, val);
        }
        lua_pop(L, 2);
    }
}

int HttpClientModule::luaClientRequest(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "url");
    const char* url = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "method");
    const char* method = lua_isstring(L, -1) ? lua_tostring(L, -1) : "GET";
    lua_pop(L, 1);

    std::map<std::string, std::string> headers;
    lua_getfield(L, 1, "headers");
    if (lua_istable(L, -1)) {
        readHeadersTable(L, lua_absindex(L, -1), headers);
    }
    lua_pop(L, 1);

    std::string body;
    lua_getfield(L, 1, "body");
    if (lua_isstring(L, -1)) {
        size_t len = 0;
        const char* chunk = lua_tolstring(L, -1, &len);
        if (chunk != nullptr) {
            body.assign(chunk, len);
        }
    }
    lua_pop(L, 1);

    varn::http::client::ClientRequestOptions options;

    lua_getfield(L, 1, "timeoutSeconds");
    if (lua_isinteger(L, -1)) {
        const int value = static_cast<int>(lua_tointeger(L, -1));
        if (value > 0) {
            options.timeoutSeconds = value;
        }
    }
    lua_pop(L, 1);

    // tls verification is on by default, with insecure as an explicit opt-in escape hatch for dev certs.
    lua_getfield(L, 1, "verifyTls");
    if (lua_isboolean(L, -1)) {
        options.verifyTls = lua_toboolean(L, -1) != 0;
    }
    lua_pop(L, 1);
    lua_getfield(L, 1, "insecure");
    if (lua_isboolean(L, -1) && lua_toboolean(L, -1) != 0) {
        options.verifyTls = false;
    }
    lua_pop(L, 1);

    lua_getfield(L, 1, "maxResponseBytes");
    if (lua_isinteger(L, -1)) {
        const long long value = lua_tointeger(L, -1);
        if (value > 0) {
            options.maxResponseBytes = static_cast<std::size_t>(value);
        }
    }
    lua_pop(L, 1);

    auto& rt = luaRuntime(L);
    auto promise = std::make_shared<Promise>(rt);
    const std::string methodStr = method;
    const std::string urlStr = url;

#if VARN_HTTP_CLIENT_EMSCRIPTEN_FETCH_ASYNC
    try {
        varn::http::client::HttpClientPerform::performAsync(
            promise, methodStr, urlStr, headers, body, options.timeoutSeconds, options.maxResponseBytes);
    } catch (const std::exception& ex) {
        promise->reject(ex.what());
    }
#else
    rt.ioPool().post([promise, methodStr, urlStr, headers, body, options] {
        try {
            promise->resolve(varn::http::client::HttpClientPerform::perform(methodStr, urlStr, headers, body, options));
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape and terminate the process.
            promise->reject("[HttpClientModule] The request failed with a non-standard error.");
        }
    });
#endif

    Promise::push(L, promise);
    return 1;
}

void HttpClientModule::registerClient(lua_State* L) {
    luaL_checktype(L, -1, LUA_TTABLE);

    lua_newtable(L);
    lua_pushcfunction(L, &HttpClientModule::luaClientRequest);
    lua_setfield(L, -2, "request");
    lua_setfield(L, -2, "client");
}

} // namespace varn::http
