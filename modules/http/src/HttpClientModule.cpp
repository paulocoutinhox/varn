#include "varn/http/HttpClientModule.h"

#include "HttpUrl.h"
#include "varn/async/Promise.h"
#include "varn/http/HttpClientPerform.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"

#include <lua.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace varn::http
{

using varn::async::Promise;
using varn::runtime::Runtime;

namespace
{
int luaUrlEncode(lua_State* L)
{
    std::size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    const std::string out = HttpUrl::encode(std::string_view(data, len));
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int luaUrlDecode(lua_State* L)
{
    std::size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    const std::string out = HttpUrl::decode(std::string_view(data, len));
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}
} // namespace

// lua surface wrapping the raw wire primitive into an ergonomic response plus query/json options.
static const char* const kClientPrelude = R"lua(
local client = ...
local requestRaw = client.requestRaw
local async = require("async")

local ok, json = pcall(require, "json")
if not ok then
    json = nil
end

-- percent-encode a query component so keys and values survive transport unambiguously.
local function encodeComponent(value)
    return (tostring(value):gsub("[^%w%-%_%.%~]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

-- fold a { k = v } table into a sorted query string for stable, reproducible urls.
local function buildQuery(params)
    local keys = {}
    for key in pairs(params) do
        keys[#keys + 1] = key
    end
    table.sort(keys)

    local pairsOut = {}
    for _, key in ipairs(keys) do
        pairsOut[#pairsOut + 1] = encodeComponent(key) .. "=" .. encodeComponent(params[key])
    end

    return table.concat(pairsOut, "&")
end

-- append a query string to a url, respecting an existing '?' already present.
local function withQuery(url, params)
    local query = buildQuery(params)
    if query == "" then
        return url
    end

    local separator = url:find("?", 1, true) and "&" or "?"
    return url .. separator .. query
end

-- split the VARN/1 wire into status and raw body, the only framing the primitive returns.
local function parseWire(wire)
    local nl = wire:find("\n", 1, true)
    if not nl then
        error("http.client: missing wire header terminator")
    end

    local status, len = wire:sub(1, nl - 1):match("^VARN/1 (%d+) (%d+)$")
    if not status then
        error("http.client: bad wire header: " .. wire:sub(1, nl - 1))
    end

    return tonumber(status), wire:sub(nl + 1, nl + tonumber(len))
end

-- build the ergonomic response table with a lazily parsed json() accessor.
local function makeResponse(status, body)
    local response = {
        status = status,
        ok = status < 400,
        headers = {},
        body = body,
    }

    function response.json()
        if not json then
            error("http.client: the json module is not available")
        end
        return json.decode(body)
    end

    return response
end

-- normalize a string/table argument into request options, applying query and json shortcuts.
local function buildOptions(opts)
    opts = opts or {}

    local options = {
        url = opts.url,
        method = opts.method,
        headers = {},
        body = opts.body,
        timeoutSeconds = opts.timeoutSeconds,
        verifyTls = opts.verifyTls,
        insecure = opts.insecure,
        maxResponseBytes = opts.maxResponseBytes,
    }

    if opts.headers then
        for key, value in pairs(opts.headers) do
            options.headers[key] = value
        end
    end

    if opts.query then
        options.url = withQuery(options.url, opts.query)
    end

    -- json option serializes the body and sets the content type unless the caller already set one.
    if opts.json ~= nil then
        if not json then
            error("http.client: the json module is not available")
        end
        options.body = json.encode(opts.json)
        local hasType = false
        for key in pairs(options.headers) do
            if key:lower() == "content-type" then
                hasType = true
                break
            end
        end
        if not hasType then
            options.headers["Content-Type"] = "application/json"
        end
    end

    return options
end

function client.request(opts)
    local options = buildOptions(opts)
    return async.promise(function()
        local wire, err = requestRaw(options):await()
        if err then
            error(err, 0)
        end

        local status, body = parseWire(wire)
        return makeResponse(status, body)
    end)
end

function client.get(url, opts)
    opts = opts or {}
    opts.url = url
    opts.method = "GET"
    return client.request(opts)
end

function client.post(url, opts)
    opts = opts or {}
    opts.url = url
    opts.method = "POST"
    return client.request(opts)
end
)lua";

Runtime& HttpClientModule::luaRuntime(lua_State* L)
{
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

void HttpClientModule::readHeadersTable(lua_State* L, int absIndex, std::map<std::string, std::string>& out)
{
    lua_pushnil(L);
    while (lua_next(L, absIndex) != 0)
    {
        // coerce a copy of the key so lua_next still sees the original on the next step.
        lua_pushvalue(L, -2);
        std::size_t keyLen = 0;
        std::size_t valLen = 0;
        const char* key = lua_tolstring(L, -1, &keyLen);
        const char* val = lua_tolstring(L, -2, &valLen);
        if (key != nullptr && val != nullptr)
        {
            out.emplace(std::string(key, keyLen), std::string(val, valLen));
        }

        lua_pop(L, 2);
    }
}

int HttpClientModule::luaClientRequest(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "url");
    const char* url = luaL_checkstring(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "method");
    const char* method = lua_isstring(L, -1) ? lua_tostring(L, -1) : "GET";
    lua_pop(L, 1);

    std::map<std::string, std::string> headers;
    lua_getfield(L, 1, "headers");
    if (lua_istable(L, -1))
    {
        readHeadersTable(L, lua_absindex(L, -1), headers);
    }

    lua_pop(L, 1);

    std::string body;
    lua_getfield(L, 1, "body");
    if (lua_isstring(L, -1))
    {
        size_t len = 0;
        const char* chunk = lua_tolstring(L, -1, &len);
        if (chunk != nullptr)
        {
            body.assign(chunk, len);
        }
    }

    lua_pop(L, 1);

    varn::http::client::ClientRequestOptions options;

    lua_getfield(L, 1, "timeoutSeconds");
    if (lua_isinteger(L, -1))
    {
        const int value = static_cast<int>(lua_tointeger(L, -1));
        if (value > 0)
        {
            options.timeoutSeconds = value;
        }
    }

    lua_pop(L, 1);

    // tls verification is on by default, with insecure as an explicit opt-in escape hatch for dev certs.
    lua_getfield(L, 1, "verifyTls");
    if (lua_isboolean(L, -1))
    {
        options.verifyTls = lua_toboolean(L, -1) != 0;
    }

    lua_pop(L, 1);
    lua_getfield(L, 1, "insecure");
    if (lua_isboolean(L, -1) && lua_toboolean(L, -1) != 0)
    {
        options.verifyTls = false;
    }

    lua_pop(L, 1);

    lua_getfield(L, 1, "maxResponseBytes");
    if (lua_isinteger(L, -1))
    {
        const long long value = lua_tointeger(L, -1);
        if (value > 0)
        {
            options.maxResponseBytes = static_cast<std::size_t>(value);
        }
    }

    lua_pop(L, 1);

    auto& rt = luaRuntime(L);
    auto promise = std::make_shared<Promise>(rt);
    const std::string methodStr = method;
    const std::string urlStr = url;

#if VARN_HTTP_CLIENT_EMSCRIPTEN_FETCH_ASYNC
    try
    {
        varn::http::client::HttpClientPerform::performAsync(
            promise, methodStr, urlStr, headers, body, options.timeoutSeconds, options.maxResponseBytes);
    }
    catch (const std::exception& ex)
    {
        promise->reject(ex.what());
    }
#else
    rt.ioPool().post([promise, methodStr, urlStr, headers, body, options]
                     {
        try {
            promise->resolve(varn::http::client::HttpClientPerform::perform(methodStr, urlStr, headers, body, options));
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        } catch (...) {
            // a worker thread must never let an exception escape and terminate the process.
            promise->reject("[HttpClientModule] The request failed with a non-standard error.");
        } });
#endif

    Promise::push(L, promise);
    return 1;
}

void HttpClientModule::installPrelude(lua_State* L)
{
    if (luaL_loadstring(L, kClientPrelude) != LUA_OK)
    {
        const char* message = lua_tostring(L, -1);
        luaL_error(L, "[HttpClientModule] The client prelude failed to compile: %s", message ? message : "");
    }

    // pass the client table currently on top of the stack to the prelude as its single argument.
    lua_pushvalue(L, -2);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
    {
        const char* message = lua_tostring(L, -1);
        luaL_error(L, "[HttpClientModule] The client prelude failed to run: %s", message ? message : "");
    }
}

void HttpClientModule::registerClient(lua_State* L)
{
    luaL_checktype(L, -1, LUA_TTABLE);

    lua_pushcfunction(L, &luaUrlEncode);
    lua_setfield(L, -2, "urlEncode");

    lua_pushcfunction(L, &luaUrlDecode);
    lua_setfield(L, -2, "urlDecode");

    lua_newtable(L);

    // the raw primitive returns a promise resolving to the VARN/1 wire string and the prelude layers ergonomics over it.
    lua_pushcfunction(L, &HttpClientModule::luaClientRequest);
    lua_setfield(L, -2, "requestRaw");

    installPrelude(L);
    lua_setfield(L, -2, "client");
}

} // namespace varn::http
