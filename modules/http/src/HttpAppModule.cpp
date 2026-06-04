#include "varn/http/HttpAppModule.h"
#include "varn/http/HttpServerModule.h"
#include "varn/http/HttpTypes.h"
#include "varn/http/MimeTypes.h"
#include "varn/http/Router.h"
#include "varn/http/StaticFileHandler.h"
#include "varn/http/drivers/poco/PocoHttpServer.h"

#include "HttpMultipart.h"
#include "HttpText.h"
#include "HttpToken.h"
#include "HttpUrlForm.h"
#include "varn/crypto/CryptoPrimitives.h"
#include "varn/log/Log.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"
#if VARN_JSON_DRIVER_NLOHMANN
#include "varn/json/JsonSerializer.h"
#endif
#if VARN_XML_DRIVER_PUGIXML
#include "varn/xml/XmlSerializer.h"
#endif

#include <Poco/Exception.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Timespan.h>
#include <Poco/URI.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <future>
#include <chrono>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <lua.hpp>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace varn::http {

using varn::lua::LuaHelpers;
using varn::runtime::Runtime;

constexpr const char* kAppMeta = "varn.HttpApp";
constexpr const char* kGroupMeta = "varn.HttpGroup";
constexpr const char* kRouteMeta = "varn.HttpRoute";
constexpr const char* kContextMeta = "varn.HttpContext";
constexpr const char* kWsConnMeta = "varn.HttpWsConn";

struct Group {
    std::string fullPrefix;
    std::vector<int> middleware;
    int parentId = -1;
};

struct RouteEntry {
    int groupId = 0;
    std::vector<int> middleware;
    int handlerRef = LUA_NOREF;
};

struct SessionEntry {
    int dataRef = LUA_NOREF;
    long long lastAccessMs = 0;
};

struct WsRoute {
    std::string pattern;
    int openRef = LUA_NOREF;
    int messageRef = LUA_NOREF;
    int closeRef = LUA_NOREF;
    std::vector<std::string> allowedOrigins;
};

// caps an assembled websocket message so a fragmented stream cannot exhaust memory.
constexpr std::size_t kWsMaxMessageBytes = 1u << 20;

// shared between the transport thread that owns the socket and the main loop that runs lua callbacks.
struct WsConnState {
    std::mutex mutex;
    std::deque<std::string> outgoing;
    bool closed = false;
    int luaRef = LUA_NOREF;
};

struct AppState {
    Router router;
    Runtime* runtime = nullptr;
    lua_State* luaMain = nullptr;
    std::vector<Group> groups;
    std::vector<RouteEntry> routes;
    int errorHandlerRef = LUA_NOREF;
    int notFoundHandlerRef = LUA_NOREF;
    std::unordered_map<std::string, SessionEntry> sessions;
    std::string sessionCookie = "varn_session";
    long long sessionTtlMs = 86400000;
    std::size_t maxSessions = 100000;
    std::string csrfSecret;
    bool tls = false;
    std::unique_ptr<StaticFileHandler> staticFiles;
    std::unordered_map<std::string, std::vector<int>> events;
    std::vector<int> startHooks;
    std::vector<int> requestHooks;
    std::vector<int> responseHooks;
    std::vector<WsRoute> wsRoutes;
    // live websocket connections, tracked on the loop thread so their registry refs are released
    // deterministically here even if a session is cut short by shutdown before wsRelease runs.
    std::vector<std::shared_ptr<WsConnState>> liveWsConns;
    int configRef = LUA_NOREF;

    ~AppState() {
        if (!luaMain) {
            return;
        }

        for (Group& group : groups) {
            for (int ref : group.middleware) {
                luaL_unref(luaMain, LUA_REGISTRYINDEX, ref);
            }
        }

        for (RouteEntry& route : routes) {
            for (int ref : route.middleware) {
                luaL_unref(luaMain, LUA_REGISTRYINDEX, ref);
            }
            luaL_unref(luaMain, LUA_REGISTRYINDEX, route.handlerRef);
        }

        for (auto& [id, entry] : sessions) {
            luaL_unref(luaMain, LUA_REGISTRYINDEX, entry.dataRef);
        }

        for (auto& [name, handlers] : events) {
            for (int ref : handlers) {
                luaL_unref(luaMain, LUA_REGISTRYINDEX, ref);
            }
        }

        for (int ref : startHooks) {
            luaL_unref(luaMain, LUA_REGISTRYINDEX, ref);
        }

        for (int ref : requestHooks) {
            luaL_unref(luaMain, LUA_REGISTRYINDEX, ref);
        }

        for (int ref : responseHooks) {
            luaL_unref(luaMain, LUA_REGISTRYINDEX, ref);
        }

        for (WsRoute& route : wsRoutes) {
            luaL_unref(luaMain, LUA_REGISTRYINDEX, route.openRef);
            luaL_unref(luaMain, LUA_REGISTRYINDEX, route.messageRef);
            luaL_unref(luaMain, LUA_REGISTRYINDEX, route.closeRef);
        }

        // release any websocket connection ref whose session ended without wsRelease running.
        for (const auto& conn : liveWsConns) {
            if (conn && conn->luaRef != LUA_NOREF) {
                luaL_unref(luaMain, LUA_REGISTRYINDEX, conn->luaRef);
                conn->luaRef = LUA_NOREF;
            }
        }

        luaL_unref(luaMain, LUA_REGISTRYINDEX, configRef);
        luaL_unref(luaMain, LUA_REGISTRYINDEX, errorHandlerRef);
        luaL_unref(luaMain, LUA_REGISTRYINDEX, notFoundHandlerRef);
    }
};

struct AppUserdata {
    std::shared_ptr<AppState> state;
};

struct GroupUserdata {
    std::shared_ptr<AppState> state;
    int groupId = 0;
};

struct RouteUserdata {
    std::shared_ptr<AppState> state;
    int routeId = -1;
};

struct ContextUserdata {
    std::shared_ptr<HttpResponse> response;
    std::shared_ptr<AppState> app;
    std::string contentType;
    int threadRef = LUA_NOREF;
    int chainRef = LUA_NOREF;
    int selfRef = LUA_NOREF;
    std::string sessionId;
};

struct WsConnUserdata {
    std::shared_ptr<WsConnState> conn;
};

struct Target {
    std::shared_ptr<AppState> state;
    int groupId = 0;
};

class HttpApp {
public:
    static AppUserdata* checkApp(lua_State* L);
    static RouteUserdata* checkRoute(lua_State* L);
    static ContextUserdata* checkContext(lua_State* L);
    static Target resolveTarget(lua_State* L);
    static void pushRoute(lua_State* L, std::shared_ptr<AppState> state, int routeId);
    static std::string findContentType(const HttpRequest& request);
    static long long nowMs();
    static std::string randomSessionId();
    static void sweepSessions(AppState& app, lua_State* L, long long now);
    static void evictOldestSession(AppState& app, lua_State* L);
    static int luaContextStatus(lua_State* L);
    static int luaContextHeader(lua_State* L);
    static int luaContextType(lua_State* L);
    static int luaContextText(lua_State* L);
    static int luaContextHtml(lua_State* L);
    static int luaContextWrite(lua_State* L);
    static int luaContextSend(lua_State* L);
    static int luaContextRedirect(lua_State* L);
    static bool invalidCookieToken(const std::string& token);
    static bool invalidCookieAttribute(const std::string& value);
    static int luaContextCookie(lua_State* L);
    static int luaContextBody(lua_State* L);
    static int luaContextSession(lua_State* L);
    static int luaContextRegenerateSession(lua_State* L);
    static std::string contentDispositionAttachment(const std::string& filename);
    static int luaContextFile(lua_State* L);
    static int luaContextJson(lua_State* L);
    static int luaContextXml(lua_State* L);
    static int luaContextIndex(lua_State* L);
    static int luaContextGc(lua_State* L);
    static int terminalNotFound(lua_State* L);
    static int terminalMethodNotAllowed(lua_State* L);
    static int terminalAutoOptions(lua_State* L);
    static int chainContinue(lua_State* L, int status, lua_KContext ctx);
    static int chainNext(lua_State* L);
    static void pushContext(lua_State* L, std::shared_ptr<AppState> app, const HttpRequest& request, const MatchResult& match, std::shared_ptr<HttpResponse> response);
    static int buildChain(lua_State* L, int chainIndex, const std::shared_ptr<AppState>& app, const MatchResult& match, const std::string& method);
    static int dispatchFinalize(lua_State* L, int status, lua_KContext kctx);
    static int dispatchBody(lua_State* L);
    static void runDispatch(const std::shared_ptr<AppState>& app, const HttpRequest& request, std::shared_ptr<HttpResponse> response);
    static std::string optString(lua_State* L, int optsIdx, const char* key, const std::string& fallback);
    static bool optBool(lua_State* L, int optsIdx, const char* key, bool fallback);
    static long long optInt(lua_State* L, int optsIdx, const char* key, long long fallback);
    static std::string requestField(lua_State* L, const char* field);
    static std::string requestHeaderCI(lua_State* L, const std::string& name);
    static std::string clientIp(lua_State* L, bool trustProxy);
    static void continueChain(lua_State* L);
    static int corsMiddleware(lua_State* L);
    static int securityHeadersMiddleware(lua_State* L);
    static int apiKeyMiddleware(lua_State* L);
    static int rateLimitMiddleware(lua_State* L);
    static int luaCors(lua_State* L);
    static int luaSecurityHeaders(lua_State* L);
    static int luaApiKey(lua_State* L);
    static int luaRateLimit(lua_State* L);
    static int luaJwtSign(lua_State* L);
    static int luaJwtVerify(lua_State* L);
    static int jwtAuthMiddleware(lua_State* L);
    static int requireAuthMiddleware(lua_State* L);
    static int requireRoleMiddleware(lua_State* L);
    static int csrfMiddleware(lua_State* L);
    static int luaJwtAuth(lua_State* L);
    static int luaRequireAuth(lua_State* L);
    static int luaRequireRole(lua_State* L);
    static int luaCsrf(lua_State* L);
    static WsConnUserdata* checkWsConn(lua_State* L);
    static int luaWsSend(lua_State* L);
    static int luaWsClose(lua_State* L);
    static int luaWsGc(lua_State* L);
    static void pushWsConn(lua_State* L, std::shared_ptr<WsConnState> conn);
    static bool wsClosed(const std::shared_ptr<WsConnState>& conn);
    static void wsFlush(Poco::Net::WebSocket& socket, const std::shared_ptr<WsConnState>& conn);
    static void waitForMainLoop(std::future<void>& ready, const std::shared_ptr<std::atomic<bool>>& stopping);
    static void wsInvoke(const std::shared_ptr<AppState>& app, int ref, std::shared_ptr<WsConnState> conn, bool hasData, std::string data, const std::shared_ptr<std::atomic<bool>>& stopping);
    static void wsRelease(const std::shared_ptr<AppState>& app, std::shared_ptr<WsConnState> conn, const std::shared_ptr<std::atomic<bool>>& stopping);
    static void runWebSocketSession(std::shared_ptr<AppState> app, Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response, std::shared_ptr<std::atomic<bool>> stopping);
    static int luaWs(lua_State* L);
    static int registerRoute(lua_State* L, const std::string& method, int pathIndex);
    static int luaVerb(lua_State* L);
    static int luaRouteMethod(lua_State* L);
    static int luaUse(lua_State* L);
    static int luaGroup(lua_State* L);
    static int luaRouteName(lua_State* L);
    static int luaRouteWhere(lua_State* L);
    static int luaOnError(lua_State* L);
    static int luaOnNotFound(lua_State* L);
    static int luaUrl(lua_State* L);
    static int luaPlugin(lua_State* L);
    static int luaModule(lua_State* L);
    static int luaOn(lua_State* L);
    static int luaEmit(lua_State* L);
    static int luaOnStart(lua_State* L);
    static int luaOnRequest(lua_State* L);
    static int luaOnResponse(lua_State* L);
    static int luaConfig(lua_State* L);
    static int luaAppListen(lua_State* L);
    static int luaAppGc(lua_State* L);
    static int luaGroupGc(lua_State* L);
    static int luaRouteGc(lua_State* L);
    static void installVerbs(lua_State* L);
    static void createMetatables(lua_State* L);
    static int luaCreateApp(lua_State* L);
};

AppUserdata* HttpApp::checkApp(lua_State* L) {
    return static_cast<AppUserdata*>(luaL_checkudata(L, 1, kAppMeta));
}

RouteUserdata* HttpApp::checkRoute(lua_State* L) {
    return static_cast<RouteUserdata*>(luaL_checkudata(L, 1, kRouteMeta));
}

ContextUserdata* HttpApp::checkContext(lua_State* L) {
    return static_cast<ContextUserdata*>(luaL_checkudata(L, 1, kContextMeta));
}

Target HttpApp::resolveTarget(lua_State* L) {
    if (void* app = luaL_testudata(L, 1, kAppMeta)) {
        return Target{static_cast<AppUserdata*>(app)->state, 0};
    }
    if (void* group = luaL_testudata(L, 1, kGroupMeta)) {
        auto* userdata = static_cast<GroupUserdata*>(group);
        return Target{userdata->state, userdata->groupId};
    }

    luaL_error(L, "[HttpApp] Expected an app or a route group.");
    return Target{};
}

void HttpApp::pushRoute(lua_State* L, std::shared_ptr<AppState> state, int routeId) {
    void* memory = lua_newuserdatauv(L, sizeof(RouteUserdata), 0);
    new (memory) RouteUserdata{std::move(state), routeId};

    luaL_getmetatable(L, kRouteMeta);
    lua_setmetatable(L, -2);
}

std::string HttpApp::findContentType(const HttpRequest& request) {
    for (const auto& [name, value] : request.headers) {
        if (HttpText::iequals(name, "content-type")) {
            return value;
        }
    }
    return "";
}

long long HttpApp::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string HttpApp::randomSessionId() {
    // use the vetted csprng so session and csrf tokens are unpredictable on every platform.
    const std::string bytes = varn::crypto::CryptoPrimitives::randomBytes(16);
    std::ostringstream id;
    for (unsigned char byte : bytes) {
        id << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return id.str();
}

void HttpApp::sweepSessions(AppState& app, lua_State* L, long long now) {
    for (auto it = app.sessions.begin(); it != app.sessions.end();) {
        if (now - it->second.lastAccessMs > app.sessionTtlMs) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->second.dataRef);
            it = app.sessions.erase(it);
        } else {
            ++it;
        }
    }
}

void HttpApp::evictOldestSession(AppState& app, lua_State* L) {
    auto oldest = app.sessions.end();
    for (auto it = app.sessions.begin(); it != app.sessions.end(); ++it) {
        if (oldest == app.sessions.end() || it->second.lastAccessMs < oldest->second.lastAccessMs) {
            oldest = it;
        }
    }
    if (oldest != app.sessions.end()) {
        luaL_unref(L, LUA_REGISTRYINDEX, oldest->second.dataRef);
        app.sessions.erase(oldest);
    }
}

int HttpApp::luaContextStatus(lua_State* L) {
    auto* context = checkContext(L);
    context->response->setStatus(static_cast<int>(luaL_checkinteger(L, 2)));
    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaContextHeader(lua_State* L) {
    auto* context = checkContext(L);
    const std::string name = LuaHelpers::checkString(L, 2);
    const std::string value = LuaHelpers::checkString(L, 3);

    // an rfc 9110 field name is a token, so reject controls, whitespace and the colon separator.
    if (name.empty()) {
        return luaL_error(L, "[HttpApp] A header name is required.");
    }
    for (unsigned char c : name) {
        if (c <= 0x20 || c == 0x7f || c == ':') {
            return luaL_error(L, "[HttpApp] The header name '%s' is not a valid token.", name.c_str());
        }
    }

    context->response->setHeader(name, value);
    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaContextType(lua_State* L) {
    auto* context = checkContext(L);
    context->response->setHeader("Content-Type", LuaHelpers::checkString(L, 2));
    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaContextText(lua_State* L) {
    auto* context = checkContext(L);
    context->response->setHeader("Content-Type", "text/plain; charset=utf-8");
    context->response->end(LuaHelpers::optionalString(L, 2, ""));
    return 0;
}

int HttpApp::luaContextHtml(lua_State* L) {
    auto* context = checkContext(L);
    context->response->setHeader("Content-Type", "text/html; charset=utf-8");
    context->response->end(LuaHelpers::optionalString(L, 2, ""));
    return 0;
}

int HttpApp::luaContextWrite(lua_State* L) {
    auto* context = checkContext(L);
    size_t length = 0;
    const char* chunk = luaL_checklstring(L, 2, &length);
    context->response->write(std::string(chunk, length));
    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaContextSend(lua_State* L) {
    auto* context = checkContext(L);
    context->response->end(LuaHelpers::optionalString(L, 2, ""));
    return 0;
}

int HttpApp::luaContextRedirect(lua_State* L) {
    auto* context = checkContext(L);
    const std::string location = LuaHelpers::checkString(L, 2);
    context->response->setStatus(static_cast<int>(luaL_optinteger(L, 3, 302)));
    context->response->setHeader("Location", location);
    context->response->end("");
    return 0;
}

// rejects control chars and separators that would let a cookie value forge attributes or a second cookie.
bool HttpApp::invalidCookieToken(const std::string& token) {
    for (unsigned char c : token) {
        if (c < 0x20 || c == 0x7f || c == ';' || c == ',' || c == ' ' || c == '"' || c == '\\') {
            return true;
        }
    }
    return false;
}

// an attribute value must not carry a control or a separator that would forge a second attribute.
bool HttpApp::invalidCookieAttribute(const std::string& value) {
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f || c == ';') {
            return true;
        }
    }
    return false;
}

int HttpApp::luaContextCookie(lua_State* L) {
    auto* context = checkContext(L);
    const std::string name = LuaHelpers::checkString(L, 2);
    const std::string value = LuaHelpers::checkString(L, 3);

    if (name.empty() || invalidCookieToken(name) || invalidCookieToken(value)) {
        return luaL_error(L, "[HttpApp] The cookie name or value contains invalid characters.");
    }

    std::string cookie = name + "=" + value;

    if (lua_istable(L, 4)) {
        lua_getfield(L, 4, "path");
        if (lua_isstring(L, -1)) {
            const std::string path = lua_tostring(L, -1);
            if (invalidCookieAttribute(path)) {
                lua_pop(L, 1);
                return luaL_error(L, "[HttpApp] The cookie path contains invalid characters.");
            }
            cookie += "; Path=" + path;
        }
        lua_pop(L, 1);

        lua_getfield(L, 4, "domain");
        if (lua_isstring(L, -1)) {
            const std::string domain = lua_tostring(L, -1);
            if (invalidCookieAttribute(domain)) {
                lua_pop(L, 1);
                return luaL_error(L, "[HttpApp] The cookie domain contains invalid characters.");
            }
            cookie += "; Domain=" + domain;
        }
        lua_pop(L, 1);

        lua_getfield(L, 4, "maxAge");
        if (lua_isinteger(L, -1)) cookie += "; Max-Age=" + std::to_string(lua_tointeger(L, -1));
        lua_pop(L, 1);

        lua_getfield(L, 4, "expires");
        if (lua_isstring(L, -1)) {
            const std::string expires = lua_tostring(L, -1);
            if (invalidCookieAttribute(expires)) {
                lua_pop(L, 1);
                return luaL_error(L, "[HttpApp] The cookie expires value contains invalid characters.");
            }
            cookie += "; Expires=" + expires;
        }
        lua_pop(L, 1);

        lua_getfield(L, 4, "secure");
        const bool secure = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        // a sameSite attribute is only valid as Strict, Lax or None.
        std::string sameSite;
        lua_getfield(L, 4, "sameSite");
        if (lua_isstring(L, -1)) {
            const std::string raw = lua_tostring(L, -1);
            if (HttpText::iequals(raw, "Strict")) {
                sameSite = "Strict";
            } else if (HttpText::iequals(raw, "Lax")) {
                sameSite = "Lax";
            } else if (HttpText::iequals(raw, "None")) {
                sameSite = "None";
            } else {
                lua_pop(L, 1);
                return luaL_error(L, "[HttpApp] The cookie sameSite value must be Strict, Lax or None.");
            }
        }
        lua_pop(L, 1);

        // browsers reject a SameSite=None cookie that is not also marked Secure.
        if (sameSite == "None" && !secure) {
            return luaL_error(L, "[HttpApp] A SameSite=None cookie must also be Secure.");
        }

        if (!sameSite.empty()) {
            cookie += "; SameSite=" + sameSite;
        }
        if (secure) {
            cookie += "; Secure";
        }

        lua_getfield(L, 4, "httpOnly");
        if (lua_toboolean(L, -1)) cookie += "; HttpOnly";
        lua_pop(L, 1);
    }

    context->response->addHeader("Set-Cookie", cookie);
    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaContextBody(lua_State* L) {
    auto* context = checkContext(L);

    lua_getiuservalue(L, 1, 1);
    lua_getfield(L, -1, "body");
    std::size_t length = 0;
    const char* raw = lua_tolstring(L, -1, &length);
    const std::string body = raw ? std::string(raw, length) : std::string();
    lua_pop(L, 2);

    const std::string format = LuaHelpers::optionalString(L, 2, "");
    const std::string type = HttpText::toLower(context->contentType);

#if VARN_JSON_DRIVER_NLOHMANN
    if (format == "json" || (format.empty() && type.find("application/json") != std::string::npos)) {
        if (!json::JsonSerializer::deserialize(L, body)) {
            return luaL_error(L, "[HttpApp] The request body is not valid JSON.");
        }
        return 1;
    }
#endif

    if (format == "form" || (format.empty() && type.find("application/x-www-form-urlencoded") != std::string::npos)) {
        HttpUrlForm::pushFormUrlEncoded(L, body);
        return 1;
    }

    if (format == "multipart" || (format.empty() && type.find("multipart/form-data") != std::string::npos)) {
        const std::string boundary = HttpMultipart::extractBoundary(context->contentType);
        if (boundary.empty()) {
            return luaL_error(L, "[HttpApp] The multipart body has no boundary.");
        }
        HttpMultipart::pushMultipart(L, body, boundary);
        return 1;
    }

    lua_pushlstring(L, body.data(), body.size());
    return 1;
}

int HttpApp::luaContextSession(lua_State* L) {
    auto* context = checkContext(L);
    AppState* app = context->app.get();

    // reuse the table if this request already resolved its session.
    lua_getiuservalue(L, 1, 4);
    if (!lua_isnil(L, -1)) {
        return 1;
    }
    lua_pop(L, 1);

    const long long now = nowMs();
    sweepSessions(*app, L, now);

    std::string id;
    lua_getiuservalue(L, 1, 1);
    lua_getfield(L, -1, "cookies");
    lua_getfield(L, -1, app->sessionCookie.c_str());
    if (lua_isstring(L, -1)) {
        id = lua_tostring(L, -1);
    }
    lua_pop(L, 3);

    auto entry = app->sessions.find(id);
    if (id.empty() || entry == app->sessions.end()) {
        // start a new session and hand its id to the client.
        id = randomSessionId();

        // bound the store so anonymous traffic cannot exhaust memory.
        if (app->sessions.size() >= app->maxSessions) {
            evictOldestSession(*app, L);
        }

        lua_newtable(L);
        const int dataRef = luaL_ref(L, LUA_REGISTRYINDEX);
        entry = app->sessions.emplace(id, SessionEntry{dataRef, now}).first;

        std::string cookie = app->sessionCookie + "=" + id + "; Path=/; HttpOnly; SameSite=Lax";
        if (app->tls) {
            cookie += "; Secure";
        }
        context->response->addHeader("Set-Cookie", cookie);
    } else {
        entry->second.lastAccessMs = now;
    }

    context->sessionId = id;

    lua_rawgeti(L, LUA_REGISTRYINDEX, entry->second.dataRef);
    lua_pushvalue(L, -1);
    lua_setiuservalue(L, 1, 4);
    return 1;
}

int HttpApp::luaContextRegenerateSession(lua_State* L) {
    auto* context = checkContext(L);
    AppState* app = context->app.get();
    const long long now = nowMs();

    // resolve the session first so there is data to carry over to the new id.
    luaContextSession(L);
    lua_pop(L, 1);

    auto current = app->sessions.find(context->sessionId);
    if (current == app->sessions.end()) {
        lua_pushvalue(L, 1);
        return 1;
    }

    // move the existing data under a fresh id and drop the old one to defeat fixation.
    const int dataRef = current->second.dataRef;
    app->sessions.erase(current);

    if (app->sessions.size() >= app->maxSessions) {
        evictOldestSession(*app, L);
    }

    std::string id = randomSessionId();
    app->sessions.emplace(id, SessionEntry{dataRef, now});
    context->sessionId = id;

    std::string cookie = app->sessionCookie + "=" + id + "; Path=/; HttpOnly; SameSite=Lax";
    if (app->tls) {
        cookie += "; Secure";
    }
    context->response->addHeader("Set-Cookie", cookie);

    lua_pushvalue(L, 1);
    return 1;
}

// builds an attachment disposition with a sanitized ascii name and an rfc 5987 encoded original.
std::string HttpApp::contentDispositionAttachment(const std::string& filename) {
    static const char* hex = "0123456789ABCDEF";
    std::string fallback;
    std::string encoded;

    for (unsigned char c : filename) {
        // separators and controls must never reach the header in either form.
        if (c == '/' || c == '\\' || c < 0x20 || c == 0x7f) {
            continue;
        }
        fallback += (c == '"' || c >= 0x80) ? '_' : static_cast<char>(c);

        const bool attrChar = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                              c == '-' || c == '.' || c == '_' || c == '~';
        if (attrChar) {
            encoded += static_cast<char>(c);
            continue;
        }
        encoded += '%';
        encoded += hex[c >> 4];
        encoded += hex[c & 0x0F];
    }

    if (fallback.empty()) {
        fallback = "download";
    }
    return "attachment; filename=\"" + fallback + "\"; filename*=UTF-8''" + encoded;
}

int HttpApp::luaContextFile(lua_State* L) {
    auto* context = checkContext(L);
    const std::string path = LuaHelpers::checkString(L, 2);

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || !std::filesystem::is_regular_file(path, ec)) {
        context->response->setStatus(404);
        context->response->setHeader("Content-Type", "text/plain; charset=utf-8");
        context->response->end("Not Found");
        return 0;
    }

    context->response->setHeader("Content-Type", MimeTypes::forPath(path));

    // an optional download flag turns the response into an attachment.
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "download");
        if (lua_isstring(L, -1)) {
            context->response->setHeader("Content-Disposition", contentDispositionAttachment(lua_tostring(L, -1)));
        } else if (lua_toboolean(L, -1)) {
            const std::string filename = std::filesystem::path(path).filename().string();
            context->response->setHeader("Content-Disposition", contentDispositionAttachment(filename));
        }
        lua_pop(L, 1);
    }

    context->response->setStatus(200);
    context->response->sendFile(path, 0, size, false);
    return 0;
}

#if VARN_JSON_DRIVER_NLOHMANN
int HttpApp::luaContextJson(lua_State* L) {
    auto* context = checkContext(L);
    const std::string body = lua_istable(L, 2) ? json::JsonSerializer::serialize(L, 2) : std::string("{}");
    context->response->setHeader("Content-Type", "application/json; charset=utf-8");
    context->response->end(body);
    return 0;
}
#endif

#if VARN_XML_DRIVER_PUGIXML
int HttpApp::luaContextXml(lua_State* L) {
    auto* context = checkContext(L);
    const std::string body =
        lua_istable(L, 2) ? xml::XmlSerializer::serialize(L, 2) : std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>\n");
    context->response->setHeader("Content-Type", "application/xml; charset=utf-8");
    context->response->end(body);
    return 0;
}
#endif

int HttpApp::luaContextIndex(lua_State* L) {
    luaL_checkudata(L, 1, kContextMeta);
    const char* key = lua_tostring(L, 2);
    if (!key) {
        lua_pushnil(L);
        return 1;
    }

    if (std::strcmp(key, "params") == 0) {
        lua_getiuservalue(L, 1, 2);
        return 1;
    }
    if (std::strcmp(key, "state") == 0) {
        lua_getiuservalue(L, 1, 3);
        return 1;
    }
    if (std::strcmp(key, "req") == 0 || std::strcmp(key, "request") == 0) {
        lua_getiuservalue(L, 1, 1);
        return 1;
    }

    if (std::strcmp(key, "query") == 0 || std::strcmp(key, "method") == 0 || std::strcmp(key, "path") == 0) {
        lua_getiuservalue(L, 1, 1);
        lua_getfield(L, -1, key);
        lua_remove(L, -2);
        return 1;
    }

    if (std::strcmp(key, "status") == 0) { lua_pushcfunction(L, &luaContextStatus); return 1; }
    if (std::strcmp(key, "header") == 0) { lua_pushcfunction(L, &luaContextHeader); return 1; }
    if (std::strcmp(key, "type") == 0) { lua_pushcfunction(L, &luaContextType); return 1; }
    if (std::strcmp(key, "text") == 0) { lua_pushcfunction(L, &luaContextText); return 1; }
    if (std::strcmp(key, "html") == 0) { lua_pushcfunction(L, &luaContextHtml); return 1; }
    if (std::strcmp(key, "redirect") == 0) { lua_pushcfunction(L, &luaContextRedirect); return 1; }
    if (std::strcmp(key, "write") == 0) { lua_pushcfunction(L, &luaContextWrite); return 1; }
    if (std::strcmp(key, "cookie") == 0) { lua_pushcfunction(L, &luaContextCookie); return 1; }
    if (std::strcmp(key, "body") == 0) { lua_pushcfunction(L, &luaContextBody); return 1; }
    if (std::strcmp(key, "session") == 0) { lua_pushcfunction(L, &luaContextSession); return 1; }
    if (std::strcmp(key, "regenerateSession") == 0) { lua_pushcfunction(L, &luaContextRegenerateSession); return 1; }
    if (std::strcmp(key, "file") == 0) { lua_pushcfunction(L, &luaContextFile); return 1; }
    if (std::strcmp(key, "send") == 0 || std::strcmp(key, "finish") == 0) { lua_pushcfunction(L, &luaContextSend); return 1; }
#if VARN_JSON_DRIVER_NLOHMANN
    if (std::strcmp(key, "json") == 0) { lua_pushcfunction(L, &luaContextJson); return 1; }
#endif
#if VARN_XML_DRIVER_PUGIXML
    if (std::strcmp(key, "xml") == 0) { lua_pushcfunction(L, &luaContextXml); return 1; }
#endif

    lua_pushnil(L);
    return 1;
}

int HttpApp::luaContextGc(lua_State* L) {
    auto* context = checkContext(L);
    context->~ContextUserdata();
    return 0;
}

int HttpApp::terminalNotFound(lua_State* L) {
    auto* context = checkContext(L);
    context->response->setStatus(404);
    context->response->setHeader("Content-Type", "text/plain; charset=utf-8");
    context->response->end("Not Found");
    return 0;
}

int HttpApp::terminalMethodNotAllowed(lua_State* L) {
    auto* context = checkContext(L);
    const char* allow = lua_tostring(L, lua_upvalueindex(1));
    context->response->setStatus(405);
    if (allow) {
        context->response->setHeader("Allow", allow);
    }
    context->response->setHeader("Content-Type", "text/plain; charset=utf-8");
    context->response->end("Method Not Allowed");
    return 0;
}

// answers an unhandled OPTIONS request for a known path by advertising the supported methods.
int HttpApp::terminalAutoOptions(lua_State* L) {
    auto* context = checkContext(L);
    const char* allow = lua_tostring(L, lua_upvalueindex(1));
    context->response->setStatus(204);
    if (allow) {
        context->response->setHeader("Allow", allow);
    }
    context->response->end("");
    return 0;
}

// continuation for lua_callk so an await inside a middleware can yield across the chain.
int HttpApp::chainContinue(lua_State* L, int status, lua_KContext ctx) {
    (void)L;
    (void)status;
    (void)ctx;
    return 0;
}

// upvalues: 1 = chain table, 2 = context, 3 = entry count, 4 = current index.
int HttpApp::chainNext(lua_State* L) {
    // single-fire guard (upvalue 5 is a one-slot flag table): a middleware that calls next() more than
    // once must not re-run the rest of the chain, which would double session writes, Set-Cookie headers,
    // rate-limit increments, and so on.
    lua_rawgeti(L, lua_upvalueindex(5), 1);
    const bool alreadyFired = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (alreadyFired) {
        return 0;
    }
    lua_pushboolean(L, 1);
    lua_rawseti(L, lua_upvalueindex(5), 1);

    const int index = static_cast<int>(lua_tointeger(L, lua_upvalueindex(4)));
    const int count = static_cast<int>(lua_tointeger(L, lua_upvalueindex(3)));

    if (index > count) {
        return 0;
    }

    lua_rawgeti(L, lua_upvalueindex(1), index);
    lua_pushvalue(L, lua_upvalueindex(2));

    // the last entry is the handler and runs without a next.
    if (index == count) {
        lua_callk(L, 1, 0, 0, &chainContinue);
        return 0;
    }

    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_pushvalue(L, lua_upvalueindex(3));
    lua_pushinteger(L, index + 1);
    lua_newtable(L); // fresh single-fire flag for the next link
    lua_pushcclosure(L, &chainNext, 5);

    lua_callk(L, 2, 0, 0, &chainContinue);
    return 0;
}

void HttpApp::pushContext(lua_State* L, std::shared_ptr<AppState> app, const HttpRequest& request, const MatchResult& match,
                 std::shared_ptr<HttpResponse> response) {
    void* memory = lua_newuserdatauv(L, sizeof(ContextUserdata), 4);
    new (memory) ContextUserdata{std::move(response), std::move(app), findContentType(request), LUA_NOREF, LUA_NOREF, LUA_NOREF, std::string()};

    luaL_getmetatable(L, kContextMeta);
    lua_setmetatable(L, -2);

    HttpServerModule::pushRequestTable(L, request);
    lua_setiuservalue(L, -2, 1);

    lua_createtable(L, 0, static_cast<int>(match.params.size()));
    for (const RouteParam& param : match.params) {
        lua_pushlstring(L, param.value.data(), param.value.size());
        lua_setfield(L, -2, param.name.c_str());
    }
    lua_setiuservalue(L, -2, 2);

    lua_newtable(L);
    lua_setiuservalue(L, -2, 3);
}

// builds the ordered chain (middleware then a terminal) into the table at chainIndex and returns its length.
int HttpApp::buildChain(lua_State* L, int chainIndex, const std::shared_ptr<AppState>& app, const MatchResult& match,
               const std::string& method) {
    int length = 0;

    auto append = [&](int ref) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_rawseti(L, chainIndex, ++length);
    };

    if (match.status == MatchStatus::Found) {
        const RouteEntry& route = app->routes[match.routeId];

        std::vector<int> ancestry;
        for (int group = route.groupId; group != -1; group = app->groups[group].parentId) {
            ancestry.push_back(group);
        }

        for (auto it = ancestry.rbegin(); it != ancestry.rend(); ++it) {
            for (int ref : app->groups[*it].middleware) {
                append(ref);
            }
        }
        for (int ref : route.middleware) {
            append(ref);
        }

        lua_rawgeti(L, LUA_REGISTRYINDEX, route.handlerRef);
        lua_rawseti(L, chainIndex, ++length);
        return length;
    }

    for (int ref : app->groups[0].middleware) {
        append(ref);
    }

    if (match.status == MatchStatus::MethodNotAllowed) {
        // advertise the supported methods, including the HEAD and OPTIONS the server answers automatically.
        std::vector<std::string> methods = match.allowedMethods;
        const bool hasGet = std::find(methods.begin(), methods.end(), "GET") != methods.end();
        if (hasGet && std::find(methods.begin(), methods.end(), "HEAD") == methods.end()) {
            methods.push_back("HEAD");
        }
        if (std::find(methods.begin(), methods.end(), "OPTIONS") == methods.end()) {
            methods.push_back("OPTIONS");
        }

        std::string allow;
        for (std::size_t i = 0; i < methods.size(); ++i) {
            if (i > 0) {
                allow += ", ";
            }
            allow += methods[i];
        }
        lua_pushlstring(L, allow.data(), allow.size());

        // an unhandled OPTIONS for a known path is answered with the method list instead of a 405.
        lua_pushcclosure(L, method == "OPTIONS" ? &terminalAutoOptions : &terminalMethodNotAllowed, 1);
        lua_rawseti(L, chainIndex, ++length);
        return length;
    }

    if (app->notFoundHandlerRef != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, app->notFoundHandlerRef);
    } else {
        lua_pushcfunction(L, &terminalNotFound);
    }
    lua_rawseti(L, chainIndex, ++length);
    return length;
}

// runs once when the chain completes, whether synchronously or after an awaited promise resumes it.
int HttpApp::dispatchFinalize(lua_State* L, int status, lua_KContext kctx) {
    const int contextRef = static_cast<int>(kctx);
    const bool failed = status != LUA_OK && status != LUA_YIELD;

    std::string detail;
    if (failed) {
        const char* message = lua_tostring(L, -1);
        detail = message ? message : "A request handler failed without a message.";
        lua_pop(L, 1);
        log::Log::error("HttpApp", detail);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, contextRef);
    auto* context = static_cast<ContextUserdata*>(luaL_checkudata(L, -1, kContextMeta));
    std::shared_ptr<HttpResponse> response = context->response;
    std::shared_ptr<AppState> app = context->app;
    const int threadRef = context->threadRef;
    const int chainRef = context->chainRef;
    lua_pop(L, 1);

    // a failed request runs the central error handler, which must stay synchronous.
    if (failed && app->errorHandlerRef != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, app->errorHandlerRef);
        lua_rawgeti(L, LUA_REGISTRYINDEX, contextRef);
        lua_pushlstring(L, detail.data(), detail.size());
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            const char* secondary = lua_tostring(L, -1);
            log::Log::error("HttpApp", secondary ? secondary : "The error handler failed.");
            lua_pop(L, 1);
        }
    }

    if (!response->ended()) {
        response->setStatus(failed ? 500 : 204);
        if (failed) {
            response->setHeader("Content-Type", "text/plain; charset=utf-8");
        }
        response->end(failed ? "Internal server error." : "");
    }

    // onResponse hooks always run once the outcome is decided, even on errors or unmatched routes.
    for (int ref : app->responseHooks) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_rawgeti(L, LUA_REGISTRYINDEX, contextRef);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char* hookError = lua_tostring(L, -1);
            log::Log::error("HttpApp", hookError ? hookError : "An onResponse hook failed.");
            lua_pop(L, 1);
        }
    }

    luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
    luaL_unref(L, LUA_REGISTRYINDEX, chainRef);
    luaL_unref(L, LUA_REGISTRYINDEX, contextRef);
    return 0;
}

// the coroutine body protects the chain so an error or a late completion still finalizes the response.
int HttpApp::dispatchBody(lua_State* L) {
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_pushvalue(L, lua_upvalueindex(2));
    lua_pushvalue(L, lua_upvalueindex(3));
    lua_pushinteger(L, 1);
    lua_newtable(L); // single-fire flag for the chain's first link
    lua_pushcclosure(L, &chainNext, 5);

    const lua_KContext kctx = static_cast<lua_KContext>(lua_tointeger(L, lua_upvalueindex(4)));
    const int status = lua_pcallk(L, 0, 0, 0, kctx, &dispatchFinalize);
    return dispatchFinalize(L, status, kctx);
}

void HttpApp::runDispatch(const std::shared_ptr<AppState>& app, const HttpRequest& request, std::shared_ptr<HttpResponse> response) {
    lua_State* mainState = app->runtime->luaState();
    const MatchResult match = app->router.match(request.method, request.path);

    // routes win, then static files answer unmatched paths before the not-found handler.
    if (match.status == MatchStatus::NotFound && app->staticFiles && app->staticFiles->tryServe(request, *response)) {
        return;
    }

    lua_State* thread = lua_newthread(mainState);
    const int threadRef = luaL_ref(mainState, LUA_REGISTRYINDEX);

    pushContext(thread, app, request, match, std::move(response));
    auto* context = static_cast<ContextUserdata*>(lua_touserdata(thread, -1));
    context->threadRef = threadRef;

    lua_newtable(thread);
    const int count = buildChain(thread, lua_gettop(thread), app, match, request.method);
    context->chainRef = luaL_ref(thread, LUA_REGISTRYINDEX);

    lua_pushvalue(thread, -1);
    context->selfRef = luaL_ref(thread, LUA_REGISTRYINDEX);

    // onRequest hooks observe and set up the context before the chain runs.
    for (int ref : app->requestHooks) {
        lua_rawgeti(thread, LUA_REGISTRYINDEX, ref);
        lua_rawgeti(thread, LUA_REGISTRYINDEX, context->selfRef);
        if (lua_pcall(thread, 1, 0, 0) != LUA_OK) {
            const char* hookError = lua_tostring(thread, -1);
            log::Log::error("HttpApp", hookError ? hookError : "An onRequest hook failed.");
            lua_pop(thread, 1);
        }
    }

    // assemble the coroutine body with the chain, context, length and context ref as upvalues.
    lua_rawgeti(thread, LUA_REGISTRYINDEX, context->chainRef);
    lua_pushvalue(thread, 1);
    lua_pushinteger(thread, count);
    lua_pushinteger(thread, context->selfRef);
    lua_pushcclosure(thread, &dispatchBody, 4);
    lua_remove(thread, 1);

    int results = 0;
    const int status = lua_resume(thread, mainState, 0, &results);

    // dispatchFinalize owns cleanup, so only a dispatcher-level failure is handled here.
    if (status != LUA_OK && status != LUA_YIELD) {
        const char* message = lua_tostring(thread, -1);
        log::Log::error("HttpApp", message ? message : "The request dispatcher failed.");
    }
}

std::string HttpApp::optString(lua_State* L, int optsIdx, const char* key, const std::string& fallback) {
    if (!lua_istable(L, optsIdx)) {
        return fallback;
    }
    lua_getfield(L, optsIdx, key);
    const std::string value = lua_isstring(L, -1) ? lua_tostring(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

bool HttpApp::optBool(lua_State* L, int optsIdx, const char* key, bool fallback) {
    if (!lua_istable(L, optsIdx)) {
        return fallback;
    }
    lua_getfield(L, optsIdx, key);
    const bool value = lua_isboolean(L, -1) ? (lua_toboolean(L, -1) != 0) : fallback;
    lua_pop(L, 1);
    return value;
}

long long HttpApp::optInt(lua_State* L, int optsIdx, const char* key, long long fallback) {
    if (!lua_istable(L, optsIdx)) {
        return fallback;
    }
    lua_getfield(L, optsIdx, key);
    const long long value = lua_isnumber(L, -1) ? static_cast<long long>(lua_tointeger(L, -1)) : fallback;
    lua_pop(L, 1);
    return value;
}

std::string HttpApp::requestField(lua_State* L, const char* field) {
    lua_getiuservalue(L, 1, 1);
    lua_getfield(L, -1, field);
    const std::string value = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 2);
    return value;
}

std::string HttpApp::requestHeaderCI(lua_State* L, const std::string& name) {
    std::string result;
    lua_getiuservalue(L, 1, 1);
    lua_getfield(L, -1, "headers");
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_isstring(L, -1) && HttpText::iequals(lua_tostring(L, -2), name)) {
            result = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 2);
    return result;
}

std::string HttpApp::clientIp(lua_State* L, bool trustProxy) {
    // behind a trusted proxy the original client is the leftmost entry of x-forwarded-for.
    if (trustProxy) {
        const std::string forwarded = requestHeaderCI(L, "X-Forwarded-For");
        if (!forwarded.empty()) {
            const std::size_t comma = forwarded.find(',');
            std::string first = comma == std::string::npos ? forwarded : forwarded.substr(0, comma);
            const std::size_t begin = first.find_first_not_of(" \t");
            const std::size_t end = first.find_last_not_of(" \t");
            if (begin != std::string::npos) {
                return first.substr(begin, end - begin + 1);
            }
        }
    }

    std::string address = requestField(L, "remoteAddress");
    const std::size_t colon = address.rfind(':');
    if (colon != std::string::npos) {
        const std::string port = address.substr(colon + 1);
        bool numeric = !port.empty();
        for (char c : port) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                numeric = false;
            }
        }
        if (numeric) {
            address = address.substr(0, colon);
        }
    }
    return address;
}

void HttpApp::continueChain(lua_State* L) {
    lua_pushvalue(L, 2);
    lua_callk(L, 0, 0, 0, &chainContinue);
}

int HttpApp::corsMiddleware(lua_State* L) {
    auto* context = checkContext(L);
    const int opts = lua_upvalueindex(1);

    // resolve the allowed origin: a fixed value, or an allowlist that echoes a matching request origin.
    std::string allowOrigin = "*";
    bool originResolved = true;
    bool varyOrigin = false;

    lua_getfield(L, opts, "origin");
    if (lua_type(L, -1) == LUA_TSTRING) {
        allowOrigin = lua_tostring(L, -1);
    } else if (lua_istable(L, -1)) {
        const std::string requestOrigin = requestHeaderCI(L, "Origin");
        const int list = lua_gettop(L);
        const int count = static_cast<int>(lua_rawlen(L, list));
        originResolved = false;
        for (int i = 1; i <= count && !originResolved; ++i) {
            lua_rawgeti(L, list, i);
            if (lua_type(L, -1) == LUA_TSTRING && !requestOrigin.empty() && requestOrigin == lua_tostring(L, -1)) {
                allowOrigin = requestOrigin;
                originResolved = true;
            }
            lua_pop(L, 1);
        }
        varyOrigin = true;
    }
    lua_pop(L, 1);

    // an unmatched allowlist origin leaves the header unset so the browser blocks the response.
    if (originResolved) {
        context->response->setHeader("Access-Control-Allow-Origin", allowOrigin);
    }
    if (varyOrigin || allowOrigin != "*") {
        context->response->setHeader("Vary", "Origin");
    }

    context->response->setHeader("Access-Control-Allow-Methods", optString(L, opts, "methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS, HEAD"));
    context->response->setHeader("Access-Control-Allow-Headers", optString(L, opts, "headers", "Content-Type, Authorization"));

    if (optBool(L, opts, "credentials", false)) {
        context->response->setHeader("Access-Control-Allow-Credentials", "true");
    }

    const long long maxAge = optInt(L, opts, "maxAge", 0);
    if (maxAge > 0) {
        context->response->setHeader("Access-Control-Max-Age", std::to_string(maxAge));
    }

    const std::string expose = optString(L, opts, "exposeHeaders", "");
    if (!expose.empty()) {
        context->response->setHeader("Access-Control-Expose-Headers", expose);
    }

    // a genuine preflight carries Access-Control-Request-Method, so only those short-circuit routing.
    if (requestField(L, "method") == "OPTIONS" && !requestHeaderCI(L, "Access-Control-Request-Method").empty()) {
        context->response->setStatus(204);
        context->response->end("");
        return 0;
    }

    continueChain(L);
    return 0;
}

int HttpApp::securityHeadersMiddleware(lua_State* L) {
    auto* context = checkContext(L);
    const int opts = lua_upvalueindex(1);

    context->response->setHeader("X-Content-Type-Options", "nosniff");
    context->response->setHeader("X-Frame-Options", optString(L, opts, "frameOptions", "DENY"));
    context->response->setHeader("Referrer-Policy", optString(L, opts, "referrerPolicy", "no-referrer"));
    context->response->setHeader("X-XSS-Protection", "0");

    const std::string csp = optString(L, opts, "contentSecurityPolicy", "");
    if (!csp.empty()) {
        context->response->setHeader("Content-Security-Policy", csp);
    }

    // hsts is only meaningful over tls, so never advertise it on a plaintext listener.
    const long long hsts = optInt(L, opts, "hsts", 0);
    if (hsts > 0 && context->app->tls) {
        context->response->setHeader("Strict-Transport-Security", "max-age=" + std::to_string(hsts) + "; includeSubDomains");
    }

    continueChain(L);
    return 0;
}

int HttpApp::apiKeyMiddleware(lua_State* L) {
    auto* context = checkContext(L);
    const int opts = lua_upvalueindex(1);

    const std::string headerName = optString(L, opts, "header", "X-API-Key");
    const std::string provided = requestHeaderCI(L, headerName);

    bool authorized = false;
    if (!provided.empty() && lua_istable(L, opts)) {
        lua_getfield(L, opts, "key");
        if (lua_isstring(L, -1) && HttpToken::constantTimeEqual(provided, lua_tostring(L, -1))) {
            authorized = true;
        }
        lua_pop(L, 1);

        if (!authorized) {
            lua_getfield(L, opts, "keys");
            if (lua_istable(L, -1)) {
                const int count = static_cast<int>(lua_rawlen(L, -1));
                for (int i = 1; i <= count && !authorized; ++i) {
                    lua_rawgeti(L, -1, i);
                    if (lua_isstring(L, -1) && HttpToken::constantTimeEqual(provided, lua_tostring(L, -1))) {
                        authorized = true;
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
    }

    if (!authorized) {
        context->response->setStatus(401);
        context->response->setHeader("Content-Type", "application/json; charset=utf-8");
        context->response->end("{\"error\":\"invalid api key\"}");
        return 0;
    }

    continueChain(L);
    return 0;
}

int HttpApp::rateLimitMiddleware(lua_State* L) {
    auto* context = checkContext(L);
    const int opts = lua_upvalueindex(1);
    const int state = lua_upvalueindex(2);

    const long long windowMs = optInt(L, opts, "windowMs", 60000);
    const long long max = optInt(L, opts, "max", 100);
    const bool trustProxy = optBool(L, opts, "trustProxy", false);
    const long long now = nowMs();
    const std::string ip = clientIp(L, trustProxy);

    // keep per-ip buckets in a dedicated subtable so a client address can never collide with bookkeeping keys.
    lua_getfield(L, state, "buckets");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, state, "buckets");
    }
    const int buckets = lua_gettop(L);

    // drop expired buckets at most once per window so the per-request cost stays near constant.
    lua_getfield(L, state, "sweptAt");
    const long long sweptAt = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (now - sweptAt >= windowMs) {
        lua_pushnil(L);
        while (lua_next(L, buckets) != 0) {
            long long bucketReset = 0;
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "r");
                bucketReset = lua_tointeger(L, -1);
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            if (now >= bucketReset) {
                lua_pushvalue(L, -1);
                lua_pushnil(L);
                lua_settable(L, buckets);
            }
        }

        lua_pushinteger(L, now);
        lua_setfield(L, state, "sweptAt");
    }

    long long count = 0;
    long long reset = 0;
    lua_getfield(L, buckets, ip.c_str());
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "n");
        count = lua_tointeger(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "r");
        reset = lua_tointeger(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    if (now >= reset) {
        count = 0;
        reset = now + windowMs;
    }
    count += 1;

    lua_newtable(L);
    lua_pushinteger(L, count);
    lua_setfield(L, -2, "n");
    lua_pushinteger(L, reset);
    lua_setfield(L, -2, "r");
    lua_setfield(L, buckets, ip.c_str());

    lua_pop(L, 1);

    context->response->setHeader("X-RateLimit-Limit", std::to_string(max));
    context->response->setHeader("X-RateLimit-Remaining", std::to_string(count > max ? 0 : max - count));

    if (count > max) {
        const long long retry = (reset - now) / 1000 + 1;
        context->response->setStatus(429);
        context->response->setHeader("Retry-After", std::to_string(retry));
        context->response->setHeader("Content-Type", "application/json; charset=utf-8");
        context->response->end("{\"error\":\"too many requests\"}");
        return 0;
    }

    continueChain(L);
    return 0;
}

int HttpApp::luaCors(lua_State* L) {
    if (lua_istable(L, 1)) {
        // reject the invalid wildcard-with-credentials combination at setup time, never silently.
        lua_getfield(L, 1, "credentials");
        const bool credentials = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        lua_getfield(L, 1, "origin");
        const bool wildcardOrigin =
            lua_isnil(L, -1) || (lua_type(L, -1) == LUA_TSTRING && std::string(lua_tostring(L, -1)) == "*");
        lua_pop(L, 1);

        if (credentials && wildcardOrigin) {
            return luaL_error(L, "[HttpApp] cors credentials cannot be combined with origin '*'.");
        }

        lua_pushvalue(L, 1);
    } else {
        lua_newtable(L);
    }
    lua_pushcclosure(L, &corsMiddleware, 1);
    return 1;
}

int HttpApp::luaSecurityHeaders(lua_State* L) {
    if (lua_istable(L, 1)) {
        lua_pushvalue(L, 1);
    } else {
        lua_newtable(L);
    }
    lua_pushcclosure(L, &securityHeadersMiddleware, 1);
    return 1;
}

int HttpApp::luaApiKey(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, &apiKeyMiddleware, 1);
    return 1;
}

int HttpApp::luaRateLimit(lua_State* L) {
    if (lua_istable(L, 1)) {
        lua_pushvalue(L, 1);
    } else {
        lua_newtable(L);
    }
    lua_newtable(L);
    lua_pushcclosure(L, &rateLimitMiddleware, 2);
    return 1;
}

int HttpApp::luaJwtSign(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const std::string secret = LuaHelpers::checkString(L, 2);

    if (secret.empty()) {
        return luaL_error(L, "[HttpApp] A jwt secret is required.");
    }

    const long long now = static_cast<long long>(std::time(nullptr));
    const long long expiresIn = optInt(L, 3, "expiresIn", 0);
    const long long notBefore = optInt(L, 3, "notBefore", 0);

    // copy the caller's claims so signing never mutates the input table.
    lua_newtable(L);
    const int claims = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        lua_pushvalue(L, -2);
        lua_pushvalue(L, -2);
        lua_settable(L, claims);
        lua_pop(L, 1);
    }

    // iat is added when the caller did not set it, while exp and nbf reflect the requested window.
    lua_getfield(L, claims, "iat");
    const bool hasIat = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!hasIat) {
        lua_pushinteger(L, static_cast<lua_Integer>(now));
        lua_setfield(L, claims, "iat");
    }
    if (expiresIn > 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(now + expiresIn));
        lua_setfield(L, claims, "exp");
    }
    if (notBefore > 0) {
        lua_pushinteger(L, static_cast<lua_Integer>(now + notBefore));
        lua_setfield(L, claims, "nbf");
    }

    std::string payload;
    std::string signature;
    try {
        payload = json::JsonSerializer::serialize(L, claims);
        const std::string signingInput = HttpToken::base64UrlEncode("{\"alg\":\"HS256\",\"typ\":\"JWT\"}") + "." + HttpToken::base64UrlEncode(payload);
        signature = HttpToken::base64UrlEncode(varn::crypto::CryptoPrimitives::hmac("SHA256", secret, signingInput, false));
        const std::string token = signingInput + "." + signature;
        lua_pushlstring(L, token.data(), token.size());
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }
    return 1;
}

int HttpApp::luaJwtVerify(lua_State* L) {
    const std::string token = LuaHelpers::checkString(L, 1);
    const std::string secret = LuaHelpers::checkString(L, 2);
    const JwtVerifyOptions options = HttpToken::readJwtVerifyOptions(L, 3);

    std::string error;
    if (!HttpToken::verifyJwt(L, token, secret, options, error)) {
        lua_pushnil(L);
        lua_pushlstring(L, error.data(), error.size());
        return 2;
    }
    return 1;
}

int HttpApp::jwtAuthMiddleware(lua_State* L) {
    auto* context = checkContext(L);
    const int opts = lua_upvalueindex(1);
    const std::string secret = optString(L, opts, "secret", "");
    const JwtVerifyOptions options = HttpToken::readJwtVerifyOptions(L, opts);

    const std::string authorization = requestHeaderCI(L, "Authorization");
    std::string token;
    if (authorization.rfind("Bearer ", 0) == 0) {
        token = authorization.substr(7);
    }

    std::string error;
    if (token.empty() || !HttpToken::verifyJwt(L, token, secret, options, error)) {
        context->response->setStatus(401);
        context->response->setHeader("Content-Type", "application/json; charset=utf-8");
        context->response->end("{\"error\":\"unauthorized\"}");
        return 0;
    }

    // expose the verified claims to handlers as ctx.state.user.
    lua_getiuservalue(L, 1, 3);
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, "user");
    lua_pop(L, 2);

    continueChain(L);
    return 0;
}

int HttpApp::requireAuthMiddleware(lua_State* L) {
    auto* context = checkContext(L);

    lua_getiuservalue(L, 1, 3);
    lua_getfield(L, -1, "user");
    const bool authenticated = !lua_isnil(L, -1);
    lua_pop(L, 2);

    if (!authenticated) {
        context->response->setStatus(401);
        context->response->setHeader("Content-Type", "application/json; charset=utf-8");
        context->response->end("{\"error\":\"unauthorized\"}");
        return 0;
    }

    continueChain(L);
    return 0;
}

int HttpApp::requireRoleMiddleware(lua_State* L) {
    auto* context = checkContext(L);
    const std::string wanted = lua_tostring(L, lua_upvalueindex(1));

    lua_getiuservalue(L, 1, 3);
    lua_getfield(L, -1, "user");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        context->response->setStatus(401);
        context->response->setHeader("Content-Type", "application/json; charset=utf-8");
        context->response->end("{\"error\":\"unauthorized\"}");
        return 0;
    }

    bool allowed = false;
    lua_getfield(L, -1, "role");
    if (lua_isstring(L, -1) && wanted == lua_tostring(L, -1)) {
        allowed = true;
    }
    lua_pop(L, 1);

    if (!allowed) {
        lua_getfield(L, -1, "roles");
        if (lua_istable(L, -1)) {
            const int count = static_cast<int>(lua_rawlen(L, -1));
            for (int i = 1; i <= count && !allowed; ++i) {
                lua_rawgeti(L, -1, i);
                if (lua_isstring(L, -1) && wanted == lua_tostring(L, -1)) {
                    allowed = true;
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 2);

    if (!allowed) {
        context->response->setStatus(403);
        context->response->setHeader("Content-Type", "application/json; charset=utf-8");
        context->response->end("{\"error\":\"forbidden\"}");
        return 0;
    }

    continueChain(L);
    return 0;
}

int HttpApp::csrfMiddleware(lua_State* L) {
    auto* context = checkContext(L);
    const int opts = lua_upvalueindex(1);
    const std::string cookieName = optString(L, opts, "cookie", "csrf_token");
    const std::string headerName = optString(L, opts, "header", "X-CSRF-Token");
    const std::string method = requestField(L, "method");
    AppState* app = context->app.get();

    // a per-app secret signs every token so an injected or forged cookie cannot pass verification.
    if (app->csrfSecret.empty()) {
        try {
            app->csrfSecret = varn::crypto::CryptoPrimitives::randomBytes(32);
        } catch (const std::exception& ex) {
            return luaL_error(L, "%s", ex.what());
        }
    }

    // bind the token to the session, which also establishes the session cookie when absent.
    luaContextSession(L);
    lua_pop(L, 1);
    const std::string sessionId = context->sessionId;

    std::string cookieToken;
    lua_getiuservalue(L, 1, 1);
    lua_getfield(L, -1, "cookies");
    lua_getfield(L, -1, cookieName.c_str());
    if (lua_isstring(L, -1)) {
        cookieToken = lua_tostring(L, -1);
    }
    lua_pop(L, 3);

    const bool safeMethod = method == "GET" || method == "HEAD" || method == "OPTIONS";
    if (safeMethod) {
        bool valid = false;
        try {
            valid = !cookieToken.empty() && HttpToken::validCsrfToken(app->csrfSecret, sessionId, cookieToken);
        } catch (const std::exception&) {
            valid = false;
        }

        // issue a fresh token only when the client lacks a valid one, keeping it readable for the header echo.
        if (!valid) {
            try {
                cookieToken = HttpToken::makeCsrfToken(app->csrfSecret, sessionId);
            } catch (const std::exception& ex) {
                return luaL_error(L, "%s", ex.what());
            }
            std::string csrfCookie = cookieName + "=" + cookieToken + "; Path=/; SameSite=Lax";
            if (context->app->tls) {
                csrfCookie += "; Secure";
            }
            context->response->addHeader("Set-Cookie", csrfCookie);
        }

        lua_getiuservalue(L, 1, 3);
        lua_pushlstring(L, cookieToken.data(), cookieToken.size());
        lua_setfield(L, -2, "csrfToken");
        lua_pop(L, 1);
        continueChain(L);
        return 0;
    }

    const std::string submitted = requestHeaderCI(L, headerName);
    bool authorized = false;
    try {
        authorized = !cookieToken.empty() && !submitted.empty() && HttpToken::constantTimeEqual(cookieToken, submitted) &&
                     HttpToken::validCsrfToken(app->csrfSecret, sessionId, cookieToken);
    } catch (const std::exception&) {
        authorized = false;
    }

    if (!authorized) {
        context->response->setStatus(403);
        context->response->setHeader("Content-Type", "application/json; charset=utf-8");
        context->response->end("{\"error\":\"invalid csrf token\"}");
        return 0;
    }

    continueChain(L);
    return 0;
}

int HttpApp::luaJwtAuth(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, &jwtAuthMiddleware, 1);
    return 1;
}

int HttpApp::luaRequireAuth(lua_State* L) {
    (void)L;
    lua_pushcfunction(L, &requireAuthMiddleware);
    return 1;
}

int HttpApp::luaRequireRole(lua_State* L) {
    luaL_checkstring(L, 1);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, &requireRoleMiddleware, 1);
    return 1;
}

int HttpApp::luaCsrf(lua_State* L) {
    if (lua_istable(L, 1)) {
        lua_pushvalue(L, 1);
    } else {
        lua_newtable(L);
    }
    lua_pushcclosure(L, &csrfMiddleware, 1);
    return 1;
}

WsConnUserdata* HttpApp::checkWsConn(lua_State* L) {
    return static_cast<WsConnUserdata*>(luaL_checkudata(L, 1, kWsConnMeta));
}

int HttpApp::luaWsSend(lua_State* L) {
    auto* userdata = checkWsConn(L);
    std::size_t length = 0;
    const char* data = luaL_checklstring(L, 2, &length);
    {
        std::lock_guard<std::mutex> lock(userdata->conn->mutex);
        userdata->conn->outgoing.emplace_back(data, length);
    }
    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaWsClose(lua_State* L) {
    auto* userdata = checkWsConn(L);
    {
        std::lock_guard<std::mutex> lock(userdata->conn->mutex);
        userdata->conn->closed = true;
    }
    return 0;
}

int HttpApp::luaWsGc(lua_State* L) {
    auto* userdata = checkWsConn(L);
    userdata->conn.~shared_ptr<WsConnState>();
    return 0;
}

void HttpApp::pushWsConn(lua_State* L, std::shared_ptr<WsConnState> conn) {
    void* memory = lua_newuserdatauv(L, sizeof(WsConnUserdata), 0);
    new (memory) WsConnUserdata{std::move(conn)};
    luaL_getmetatable(L, kWsConnMeta);
    lua_setmetatable(L, -2);
}

bool HttpApp::wsClosed(const std::shared_ptr<WsConnState>& conn) {
    std::lock_guard<std::mutex> lock(conn->mutex);
    return conn->closed;
}

void HttpApp::wsFlush(Poco::Net::WebSocket& socket, const std::shared_ptr<WsConnState>& conn) {
    std::deque<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(conn->mutex);
        pending.swap(conn->outgoing);
    }
    for (const std::string& message : pending) {
        try {
            socket.sendFrame(message.data(), static_cast<int>(message.size()), Poco::Net::WebSocket::FRAME_TEXT);
        } catch (const std::exception&) {
            break;
        }
    }
}

// blocks until the posted task runs, but gives up once the server starts shutting down.
// this keeps the transport thread from deadlocking against a main loop that is being joined.
void HttpApp::waitForMainLoop(std::future<void>& ready, const std::shared_ptr<std::atomic<bool>>& stopping) {
    while (ready.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
        if (stopping && stopping->load(std::memory_order_acquire)) {
            return;
        }
    }
}

// runs a websocket callback on the main loop and waits, so lua only ever runs on its owning thread.
void HttpApp::wsInvoke(const std::shared_ptr<AppState>& app, int ref, std::shared_ptr<WsConnState> conn, bool hasData,
              std::string data, const std::shared_ptr<std::atomic<bool>>& stopping) {
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> ready = done->get_future();

    app->runtime->mainLoop().post([app, ref, conn, hasData, data, done]() {
        lua_State* L = app->runtime->luaState();
        if (conn->luaRef == LUA_NOREF) {
            pushWsConn(L, conn);
            conn->luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
            // track it on the loop thread so ~AppState can release the ref if shutdown skips wsRelease.
            app->liveWsConns.push_back(conn);
        }
        if (ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            lua_rawgeti(L, LUA_REGISTRYINDEX, conn->luaRef);
            int nargs = 1;
            if (hasData) {
                lua_pushlstring(L, data.data(), data.size());
                nargs = 2;
            }
            if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
                const char* error = lua_tostring(L, -1);
                log::Log::error("HttpApp", error ? error : "A websocket handler failed.");
                lua_pop(L, 1);
            }
        }
        done->set_value();
    });

    waitForMainLoop(ready, stopping);
}

void HttpApp::wsRelease(const std::shared_ptr<AppState>& app, std::shared_ptr<WsConnState> conn,
               const std::shared_ptr<std::atomic<bool>>& stopping) {
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> ready = done->get_future();

    app->runtime->mainLoop().post([app, conn, done]() {
        if (conn->luaRef != LUA_NOREF) {
            luaL_unref(app->runtime->luaState(), LUA_REGISTRYINDEX, conn->luaRef);
            conn->luaRef = LUA_NOREF;
        }
        auto& live = app->liveWsConns;
        live.erase(std::remove(live.begin(), live.end(), conn), live.end());
        done->set_value();
    });

    waitForMainLoop(ready, stopping);
}

void HttpApp::runWebSocketSession(std::shared_ptr<AppState> app, Poco::Net::HTTPServerRequest& request,
                         Poco::Net::HTTPServerResponse& response, std::shared_ptr<std::atomic<bool>> stopping) {
    std::string path;
    try {
        path = Poco::URI(request.getURI()).getPath();
    } catch (const std::exception&) {
        path = request.getURI();
    }
    if (path.empty()) {
        path = "/";
    }

    const WsRoute* route = nullptr;
    for (const WsRoute& candidate : app->wsRoutes) {
        if (candidate.pattern == path) {
            route = &candidate;
            break;
        }
    }
    if (!route) {
        response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
        response.setContentLength(0);
        response.send();
        return;
    }

    // reject cross-site upgrades when the route declares an origin allowlist.
    if (!route->allowedOrigins.empty()) {
        const std::string origin = request.get("Origin", "");
        bool allowed = false;
        for (const std::string& candidate : route->allowedOrigins) {
            if (candidate == origin) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
            response.setContentLength(0);
            response.send();
            return;
        }
    }

    std::unique_ptr<Poco::Net::WebSocket> socket;
    try {
        socket = std::make_unique<Poco::Net::WebSocket>(request, response);
    } catch (const std::exception& ex) {
        log::Log::error("HttpApp", std::string("The websocket handshake failed. ") + ex.what());
        return;
    }
    socket->setReceiveTimeout(Poco::Timespan(0, 200 * 1000));

    auto conn = std::make_shared<WsConnState>();

    wsInvoke(app, route->openRef, conn, false, "", stopping);
    wsFlush(*socket, conn);

    std::vector<char> buffer(65536);
    std::string fragment;
    while (!wsClosed(conn) && !stopping->load(std::memory_order_acquire)) {
        int flags = 0;
        int received = 0;
        try {
            received = socket->receiveFrame(buffer.data(), static_cast<int>(buffer.size()), flags);
        } catch (const Poco::TimeoutException&) {
            wsFlush(*socket, conn);
            continue;
        } catch (const std::exception&) {
            break;
        }

        // a negative count should not occur, but guard it so it is never cast to a huge size below.
        if (received < 0) {
            break;
        }

        const int opcode = flags & Poco::Net::WebSocket::FRAME_OP_BITMASK;

        // a zero-length read with no opcode means the peer closed the connection.
        if (received == 0 && opcode == 0) {
            break;
        }
        if (opcode == Poco::Net::WebSocket::FRAME_OP_CLOSE) {
            break;
        }
        if (opcode == Poco::Net::WebSocket::FRAME_OP_PING) {
            try {
                socket->sendFrame(buffer.data(), received,
                                  Poco::Net::WebSocket::FRAME_OP_PONG | Poco::Net::WebSocket::FRAME_FLAG_FIN);
            } catch (const std::exception&) {
                break;
            }
            continue;
        }

        // assemble continuation frames until the final fragment, bounding the total size.
        // check before appending so the buffer never overshoots the cap by a whole frame.
        if (fragment.size() + static_cast<std::size_t>(received) > kWsMaxMessageBytes) {
            break;
        }
        fragment.append(buffer.data(), static_cast<std::size_t>(received));
        if ((flags & Poco::Net::WebSocket::FRAME_FLAG_FIN) == 0) {
            continue;
        }

        wsInvoke(app, route->messageRef, conn, true, fragment, stopping);
        fragment.clear();
        wsFlush(*socket, conn);
    }

    wsInvoke(app, route->closeRef, conn, false, "", stopping);
    wsFlush(*socket, conn);
    wsRelease(app, conn, stopping);

    try {
        socket->shutdown();
    } catch (const std::exception&) {
    }
}

int HttpApp::luaWs(lua_State* L) {
    auto* app = checkApp(L);
    const std::string path = LuaHelpers::checkString(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);

    auto refField = [&](const char* name) -> int {
        lua_getfield(L, 3, name);
        if (lua_isfunction(L, -1)) {
            return luaL_ref(L, LUA_REGISTRYINDEX);
        }
        lua_pop(L, 1);
        return LUA_NOREF;
    };

    WsRoute route;
    route.pattern = path;
    route.openRef = refField("open");
    route.messageRef = refField("message");
    route.closeRef = refField("close");

    // an optional origin allowlist blocks cross-site upgrades when set.
    lua_getfield(L, 3, "origins");
    if (lua_istable(L, -1)) {
        const int count = static_cast<int>(lua_rawlen(L, -1));
        for (int i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) == LUA_TSTRING) {
                route.allowedOrigins.emplace_back(lua_tostring(L, -1));
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    app->state->wsRoutes.push_back(std::move(route));

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::registerRoute(lua_State* L, const std::string& method, int pathIndex) {
    Target target = resolveTarget(L);
    const std::string path = LuaHelpers::checkString(L, pathIndex);

    const int firstHandler = pathIndex + 1;
    const int top = lua_gettop(L);
    if (top < firstHandler) {
        return luaL_error(L, "[HttpApp] A route handler is required.");
    }
    for (int i = firstHandler; i <= top; ++i) {
        luaL_checktype(L, i, LUA_TFUNCTION);
    }

    const std::string pattern = target.state->groups[target.groupId].fullPrefix + path;
    const int routeId = target.state->router.add(method, pattern);

    RouteEntry entry;
    entry.groupId = target.groupId;
    for (int i = firstHandler; i < top; ++i) {
        lua_pushvalue(L, i);
        entry.middleware.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    }
    lua_pushvalue(L, top);
    entry.handlerRef = luaL_ref(L, LUA_REGISTRYINDEX);

    target.state->routes.push_back(std::move(entry));

    pushRoute(L, target.state, routeId);
    return 1;
}

int HttpApp::luaVerb(lua_State* L) {
    const std::string method = lua_tostring(L, lua_upvalueindex(1));
    return registerRoute(L, method, 2);
}

int HttpApp::luaRouteMethod(lua_State* L) {
    const std::string method = LuaHelpers::checkString(L, 2);
    return registerRoute(L, method, 3);
}

int HttpApp::luaUse(lua_State* L) {
    Target target = resolveTarget(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    target.state->groups[target.groupId].middleware.push_back(luaL_ref(L, LUA_REGISTRYINDEX));

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaGroup(lua_State* L) {
    Target target = resolveTarget(L);
    const std::string prefix = LuaHelpers::checkString(L, 2);

    Group group;
    group.parentId = target.groupId;
    group.fullPrefix = target.state->groups[target.groupId].fullPrefix + "/" + prefix;
    target.state->groups.push_back(std::move(group));
    const int groupId = static_cast<int>(target.state->groups.size()) - 1;

    void* memory = lua_newuserdatauv(L, sizeof(GroupUserdata), 0);
    new (memory) GroupUserdata{target.state, groupId};

    luaL_getmetatable(L, kGroupMeta);
    lua_setmetatable(L, -2);
    return 1;
}

int HttpApp::luaRouteName(lua_State* L) {
    auto* route = checkRoute(L);
    const std::string name = LuaHelpers::checkString(L, 2);

    try {
        route->state->router.setName(route->routeId, name);
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaRouteWhere(lua_State* L) {
    auto* route = checkRoute(L);
    const std::string param = LuaHelpers::checkString(L, 2);
    const std::string constraint = LuaHelpers::checkString(L, 3);

    try {
        route->state->router.setConstraint(route->routeId, param, constraint);
    } catch (const std::exception&) {
        return luaL_error(L, "[HttpApp] The constraint for '%s' is not a valid pattern.", param.c_str());
    }

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaOnError(lua_State* L) {
    auto* app = checkApp(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    luaL_unref(L, LUA_REGISTRYINDEX, app->state->errorHandlerRef);
    lua_pushvalue(L, 2);
    app->state->errorHandlerRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaOnNotFound(lua_State* L) {
    auto* app = checkApp(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    luaL_unref(L, LUA_REGISTRYINDEX, app->state->notFoundHandlerRef);
    lua_pushvalue(L, 2);
    app->state->notFoundHandlerRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaUrl(lua_State* L) {
    auto* app = checkApp(L);
    const std::string name = LuaHelpers::checkString(L, 2);

    std::unordered_map<std::string, std::string> params;
    if (lua_istable(L, 3)) {
        lua_pushnil(L);
        while (lua_next(L, 3) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const std::string key = lua_tostring(L, -2);
                const char* value = luaL_tolstring(L, -1, nullptr);
                params[key] = value ? value : "";
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
    }

    const auto url = app->state->router.buildUrl(name, params);
    if (!url) {
        return luaL_error(L, "[HttpApp] Cannot build a url for the route '%s'.", name.c_str());
    }

    lua_pushlstring(L, url->data(), url->size());
    return 1;
}

int HttpApp::luaPlugin(lua_State* L) {
    checkApp(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    const bool hasOptions = lua_gettop(L) >= 3 && !lua_isnoneornil(L, 3);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 1);
    if (hasOptions) {
        lua_pushvalue(L, 3);
        lua_call(L, 2, 0);
    } else {
        lua_call(L, 1, 0);
    }

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaModule(lua_State* L) {
    checkApp(L);
    luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    luaGroup(L);
    lua_pushvalue(L, 3);
    lua_pushvalue(L, -2);
    lua_call(L, 1, 0);
    return 1;
}

int HttpApp::luaOn(lua_State* L) {
    auto* app = checkApp(L);
    const std::string event = LuaHelpers::checkString(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);

    lua_pushvalue(L, 3);
    app->state->events[event].push_back(luaL_ref(L, LUA_REGISTRYINDEX));

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaEmit(lua_State* L) {
    auto* app = checkApp(L);
    const std::string event = LuaHelpers::checkString(L, 2);
    const int top = lua_gettop(L);

    const auto it = app->state->events.find(event);
    if (it != app->state->events.end()) {
        // copy the refs so a handler may register more handlers without invalidating the loop.
        const std::vector<int> handlers = it->second;
        for (int ref : handlers) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            for (int i = 3; i <= top; ++i) {
                lua_pushvalue(L, i);
            }
            if (lua_pcall(L, top - 2, 0, 0) != LUA_OK) {
                const char* error = lua_tostring(L, -1);
                log::Log::error("HttpApp", error ? error : "An event handler failed.");
                lua_pop(L, 1);
            }
        }
    }

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaOnStart(lua_State* L) {
    auto* app = checkApp(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    app->state->startHooks.push_back(luaL_ref(L, LUA_REGISTRYINDEX));

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaOnRequest(lua_State* L) {
    auto* app = checkApp(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    app->state->requestHooks.push_back(luaL_ref(L, LUA_REGISTRYINDEX));

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaOnResponse(lua_State* L) {
    auto* app = checkApp(L);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    app->state->responseHooks.push_back(luaL_ref(L, LUA_REGISTRYINDEX));

    lua_pushvalue(L, 1);
    return 1;
}

int HttpApp::luaConfig(lua_State* L) {
    auto* app = checkApp(L);
    const int argc = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, app->state->configRef);
    const int config = lua_gettop(L);

    // app:config(table) merges the given values into the config.
    if (argc >= 2 && lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            lua_pushvalue(L, -2);
            lua_pushvalue(L, -2);
            lua_settable(L, config);
            lua_pop(L, 1);
        }
        lua_pushvalue(L, 1);
        return 1;
    }

    // app:config(key, value) sets a single entry.
    if (argc >= 3) {
        const std::string key = LuaHelpers::checkString(L, 2);
        lua_pushvalue(L, 3);
        lua_setfield(L, config, key.c_str());
        lua_pushvalue(L, 1);
        return 1;
    }

    // app:config(key) reads a single entry.
    if (argc == 2) {
        const std::string key = LuaHelpers::checkString(L, 2);
        lua_getfield(L, config, key.c_str());
        return 1;
    }

    // app:config() returns the whole config table.
    return 1;
}

int HttpApp::luaAppListen(lua_State* L) {
    auto* app = checkApp(L);
    std::shared_ptr<AppState> state = app->state;

    HttpServerOptions options;
    if (lua_isinteger(L, 2)) {
        options.port = lua_tointeger(L, 2);
    } else if (lua_istable(L, 2)) {
        options = HttpServerModule::readListenOptions(L, 2);
    } else {
        options = HttpServerModule::readListenOptions(L, 3);
    }

    // the app owns static serving so routes always take precedence over files.
    if (options.servePublic) {
        state->staticFiles = std::make_unique<StaticFileHandler>(options.publicDir, options.directoryListing);
        options.servePublic = false;
    }

    Runtime& rt = *state->runtime;

    auto handler = [state](const HttpRequest& request, std::shared_ptr<HttpResponse> response) {
        state->runtime->mainLoop().post([state, request, response = std::move(response)]() mutable {
            runDispatch(state, request, std::move(response));
        });
    };

    const bool tls = options.tls;
    const std::string host = options.host;
    const int port = options.port;

    // remember the transport scheme so cookies and hsts can be hardened under tls.
    state->tls = tls;

    auto upgrade = [state](Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response,
                           std::shared_ptr<std::atomic<bool>> stopping) {
        runWebSocketSession(state, request, response, std::move(stopping));
    };

    auto server = std::make_shared<PocoHttpServer>(rt, std::move(options), std::move(handler), std::move(upgrade));
    server->start();
    rt.addServer(server);

    std::ostringstream started;
    started << "Listening on " << (tls ? "https" : "http") << "://" << host << ":" << port << ".";
    log::Log::line("HttpApp", started.str());

    for (int ref : state->startHooks) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_pushvalue(L, 1);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            log::Log::error("HttpApp", error ? error : "An onStart hook failed.");
            lua_pop(L, 1);
        }
    }
    return 0;
}

int HttpApp::luaAppGc(lua_State* L) {
    auto* app = checkApp(L);
    app->state.~shared_ptr<AppState>();
    return 0;
}

int HttpApp::luaGroupGc(lua_State* L) {
    auto* group = static_cast<GroupUserdata*>(luaL_checkudata(L, 1, kGroupMeta));
    group->state.~shared_ptr<AppState>();
    return 0;
}

int HttpApp::luaRouteGc(lua_State* L) {
    auto* route = checkRoute(L);
    route->state.~shared_ptr<AppState>();
    return 0;
}

void HttpApp::installVerbs(lua_State* L) {
    static constexpr const char* verbs[] = {"get", "post", "put", "patch", "delete", "options", "head", "all"};
    static constexpr const char* methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS", "HEAD", "ALL"};

    for (std::size_t i = 0; i < sizeof(verbs) / sizeof(verbs[0]); ++i) {
        lua_pushstring(L, methods[i]);
        lua_pushcclosure(L, &luaVerb, 1);
        lua_setfield(L, -2, verbs[i]);
    }
}

void HttpApp::createMetatables(lua_State* L) {
    if (luaL_newmetatable(L, kContextMeta)) {
        lua_pushcfunction(L, &luaContextGc);
        lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, &luaContextIndex);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, kWsConnMeta)) {
        lua_pushcfunction(L, &luaWsGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        lua_pushcfunction(L, &luaWsSend);
        lua_setfield(L, -2, "send");
        lua_pushcfunction(L, &luaWsClose);
        lua_setfield(L, -2, "close");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, kRouteMeta)) {
        lua_pushcfunction(L, &luaRouteGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        lua_pushcfunction(L, &luaRouteName);
        lua_setfield(L, -2, "name");
        lua_pushcfunction(L, &luaRouteWhere);
        lua_setfield(L, -2, "where");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, kGroupMeta)) {
        lua_pushcfunction(L, &luaGroupGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        installVerbs(L);
        lua_pushcfunction(L, &luaUse);
        lua_setfield(L, -2, "use");
        lua_pushcfunction(L, &luaGroup);
        lua_setfield(L, -2, "group");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, kAppMeta)) {
        lua_pushcfunction(L, &luaAppGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        installVerbs(L);
        lua_pushcfunction(L, &luaUse);
        lua_setfield(L, -2, "use");
        lua_pushcfunction(L, &luaGroup);
        lua_setfield(L, -2, "group");
        lua_pushcfunction(L, &luaRouteMethod);
        lua_setfield(L, -2, "route");
        lua_pushcfunction(L, &luaWs);
        lua_setfield(L, -2, "ws");
        lua_pushcfunction(L, &luaPlugin);
        lua_setfield(L, -2, "plugin");
        lua_pushcfunction(L, &luaModule);
        lua_setfield(L, -2, "module");
        lua_pushcfunction(L, &luaOn);
        lua_setfield(L, -2, "on");
        lua_pushcfunction(L, &luaEmit);
        lua_setfield(L, -2, "emit");
        lua_pushcfunction(L, &luaOnStart);
        lua_setfield(L, -2, "onStart");
        lua_pushcfunction(L, &luaOnRequest);
        lua_setfield(L, -2, "onRequest");
        lua_pushcfunction(L, &luaOnResponse);
        lua_setfield(L, -2, "onResponse");
        lua_pushcfunction(L, &luaConfig);
        lua_setfield(L, -2, "config");
        lua_pushcfunction(L, &luaOnError);
        lua_setfield(L, -2, "onError");
        lua_pushcfunction(L, &luaOnNotFound);
        lua_setfield(L, -2, "onNotFound");
        lua_pushcfunction(L, &luaUrl);
        lua_setfield(L, -2, "url");
        lua_pushcfunction(L, &luaAppListen);
        lua_setfield(L, -2, "listen");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

int HttpApp::luaCreateApp(lua_State* L) {
    auto& rt = *static_cast<Runtime*>(LuaHelpers::getRuntime(L));

    void* memory = lua_newuserdatauv(L, sizeof(AppUserdata), 0);
    auto* userdata = new (memory) AppUserdata{std::make_shared<AppState>()};
    userdata->state->runtime = &rt;
    userdata->state->luaMain = rt.luaState();
    userdata->state->groups.push_back(Group{"", {}, -1});

    lua_newtable(L);
    userdata->state->configRef = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, kAppMeta);
    lua_setmetatable(L, -2);
    return 1;
}

void HttpAppModule::registerApp(lua_State* L) {
    HttpApp::createMetatables(L);

    lua_pushcfunction(L, &HttpApp::luaCreateApp);
    lua_setfield(L, -2, "createApp");

    lua_pushcfunction(L, &HttpApp::luaCors);
    lua_setfield(L, -2, "cors");
    lua_pushcfunction(L, &HttpApp::luaSecurityHeaders);
    lua_setfield(L, -2, "securityHeaders");
    lua_pushcfunction(L, &HttpApp::luaApiKey);
    lua_setfield(L, -2, "apiKey");
    lua_pushcfunction(L, &HttpApp::luaRateLimit);
    lua_setfield(L, -2, "rateLimit");

    lua_newtable(L);
    lua_pushcfunction(L, &HttpApp::luaJwtSign);
    lua_setfield(L, -2, "sign");
    lua_pushcfunction(L, &HttpApp::luaJwtVerify);
    lua_setfield(L, -2, "verify");
    lua_setfield(L, -2, "jwt");

    lua_pushcfunction(L, &HttpApp::luaJwtAuth);
    lua_setfield(L, -2, "jwtAuth");
    lua_pushcfunction(L, &HttpApp::luaCsrf);
    lua_setfield(L, -2, "csrf");
    lua_pushcfunction(L, &HttpApp::luaRequireAuth);
    lua_setfield(L, -2, "requireAuth");
    lua_pushcfunction(L, &HttpApp::luaRequireRole);
    lua_setfield(L, -2, "requireRole");
}

} // namespace varn::http
