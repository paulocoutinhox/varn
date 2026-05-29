#include "varn/log/Log.h"
#include "varn/http/HttpClientModule.h"
#include "varn/http/HttpServerModule.h"
#include "varn/http/HttpTypes.h"
#include "varn/http/drivers/poco/PocoHttpServer.h"
#if VARN_JSON_DRIVER_NLOHMANN
#include "varn/json/JsonSerializer.h"
#endif
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"
#if VARN_XML_DRIVER_PUGIXML
#include "varn/xml/XmlSerializer.h"
#endif

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>

namespace varn::http {

using varn::runtime::Runtime;

namespace {

class HttpServerLuaBindings {
public:
    static int luaOpen(lua_State* L);

private:
    static constexpr const char* kServerMeta = "varn.HttpServerBuilder";
    static constexpr const char* kResponseMeta = "varn.HttpResponse";

    struct ServerBuilderUserdata {
        Runtime* runtime = nullptr;
        int handlerRef = LUA_NOREF;
    };

    struct ResponseUserdata {
        std::shared_ptr<HttpResponse> response;
    };

    static Runtime& luaRuntime(lua_State* L);
    static void pushQueryTable(lua_State* L, const std::map<std::string, std::string>& values);
    static void pushRequest(lua_State* L, const HttpRequest& request);
    static void pushResponse(lua_State* L, std::shared_ptr<HttpResponse> response);
    static ResponseUserdata* checkResponse(lua_State* L);

    static int luaResponseGc(lua_State* L);
    static int luaResponseStatus(lua_State* L);
    static int luaResponseSetHeader(lua_State* L);
    static int luaResponseFinish(lua_State* L);
#if VARN_JSON_DRIVER_NLOHMANN
    static int luaResponseJson(lua_State* L);
#endif
#if VARN_XML_DRIVER_PUGIXML
    static int luaResponseXml(lua_State* L);
#endif
    static int luaServerGc(lua_State* L);
    static HttpServerOptions readOptions(lua_State* L, int index);
    static int luaServerListen(lua_State* L);
    static int luaCreateServer(lua_State* L);
    static void createMetatables(lua_State* L);
};

Runtime& HttpServerLuaBindings::luaRuntime(lua_State* L) {
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

void HttpServerLuaBindings::pushQueryTable(lua_State* L, const std::map<std::string, std::string>& values) {
    varn::lua::LuaHelpers::pushStringMap(L, values);
}

void HttpServerLuaBindings::pushRequest(lua_State* L, const HttpRequest& request) {
    lua_newtable(L);

    lua_pushlstring(L, request.host.data(), request.host.size());
    lua_setfield(L, -2, "host");

    lua_pushlstring(L, request.method.data(), request.method.size());
    lua_setfield(L, -2, "method");

    lua_pushlstring(L, request.path.data(), request.path.size());
    lua_setfield(L, -2, "path");

    lua_pushlstring(L, request.target.data(), request.target.size());
    lua_setfield(L, -2, "target");

    lua_pushlstring(L, request.queryString.data(), request.queryString.size());
    lua_setfield(L, -2, "queryString");

    lua_pushlstring(L, request.body.data(), request.body.size());
    lua_setfield(L, -2, "body");

    lua_pushlstring(L, request.remoteAddress.data(), request.remoteAddress.size());
    lua_setfield(L, -2, "remoteAddress");

    varn::lua::LuaHelpers::pushStringMap(L, request.headers);
    lua_setfield(L, -2, "headers");

    varn::lua::LuaHelpers::pushStringMap(L, request.cookies);
    lua_setfield(L, -2, "cookies");

    pushQueryTable(L, request.query);
    lua_setfield(L, -2, "query");
}

void HttpServerLuaBindings::pushResponse(lua_State* L, std::shared_ptr<HttpResponse> response) {
    void* memory = lua_newuserdatauv(L, sizeof(ResponseUserdata), 0);
    new (memory) ResponseUserdata{std::move(response)};

    luaL_getmetatable(L, kResponseMeta);
    lua_setmetatable(L, -2);
}

HttpServerLuaBindings::ResponseUserdata* HttpServerLuaBindings::checkResponse(lua_State* L) {
    return static_cast<ResponseUserdata*>(luaL_checkudata(L, 1, kResponseMeta));
}

int HttpServerLuaBindings::luaResponseGc(lua_State* L) {
    auto* userdata = checkResponse(L);
    userdata->response.~shared_ptr<HttpResponse>();
    return 0;
}

int HttpServerLuaBindings::luaResponseStatus(lua_State* L) {
    auto* userdata = checkResponse(L);
    int status = luaL_checkinteger(L, 2);
    userdata->response->setStatus(status);
    return 0;
}

int HttpServerLuaBindings::luaResponseSetHeader(lua_State* L) {
    auto* userdata = checkResponse(L);
    std::string name = varn::lua::LuaHelpers::checkString(L, 2);
    std::string value = varn::lua::LuaHelpers::checkString(L, 3);
    userdata->response->setHeader(name, value);
    return 0;
}

int HttpServerLuaBindings::luaResponseFinish(lua_State* L) {
    auto* userdata = checkResponse(L);
    std::string body = varn::lua::LuaHelpers::optionalString(L, 2, "");
    userdata->response->end(body);
    return 0;
}

#if VARN_JSON_DRIVER_NLOHMANN
int HttpServerLuaBindings::luaResponseJson(lua_State* L) {
    auto* userdata = checkResponse(L);
    const std::string body = lua_istable(L, 2) ? json::JsonSerializer::serialize(L, 2) : "{}";

    userdata->response->setHeader("Content-Type", "application/json; charset=utf-8");
    userdata->response->end(body);
    return 0;
}
#endif

#if VARN_XML_DRIVER_PUGIXML
int HttpServerLuaBindings::luaResponseXml(lua_State* L) {
    auto* userdata = checkResponse(L);
    const std::string body =
        lua_istable(L, 2) ? xml::XmlSerializer::serialize(L, 2) : std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?><root/>\n");

    userdata->response->setHeader("Content-Type", "application/xml; charset=utf-8");
    userdata->response->end(body);
    return 0;
}
#endif

int HttpServerLuaBindings::luaServerGc(lua_State* L) {
    auto* userdata = static_cast<ServerBuilderUserdata*>(luaL_checkudata(L, 1, kServerMeta));
    if (userdata->runtime && userdata->handlerRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, userdata->handlerRef);
        userdata->handlerRef = LUA_NOREF;
    }
    return 0;
}

HttpServerOptions HttpServerLuaBindings::readOptions(lua_State* L, int index) {
    HttpServerOptions options;

    const char* envPort = std::getenv("VARN_PORT");
    if (envPort) {
        options.port = std::atoi(envPort);
    }

    const char* cert = std::getenv("VARN_TLS_CERT");
    const char* key = std::getenv("VARN_TLS_KEY");
    if (cert && key) {
        options.tls = true;
        options.certFile = cert;
        options.keyFile = key;
    }

    if (lua_istable(L, index)) {
        lua_getfield(L, index, "host");
        options.host = varn::lua::LuaHelpers::optionalString(L, -1, options.host);
        lua_pop(L, 1);

        lua_getfield(L, index, "port");
        if (lua_isinteger(L, -1)) options.port = lua_tointeger(L, -1);
        lua_pop(L, 1);

        lua_getfield(L, index, "publicDir");
        options.publicDir = varn::lua::LuaHelpers::optionalString(L, -1, options.publicDir);
        lua_pop(L, 1);

        lua_getfield(L, index, "servePublic");
        if (lua_isboolean(L, -1)) options.servePublic = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        lua_getfield(L, index, "tls");
        if (lua_isboolean(L, -1)) options.tls = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);

        lua_getfield(L, index, "certFile");
        options.certFile = varn::lua::LuaHelpers::optionalString(L, -1, options.certFile);
        lua_pop(L, 1);

        lua_getfield(L, index, "keyFile");
        options.keyFile = varn::lua::LuaHelpers::optionalString(L, -1, options.keyFile);
        lua_pop(L, 1);

        lua_getfield(L, index, "maxQueued");
        if (lua_isinteger(L, -1)) {
            const auto v = static_cast<int>(lua_tointeger(L, -1));
            if (v > 0) {
                options.maxQueued = std::clamp(v, 1, 65535);
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, index, "maxThreads");
        if (lua_isinteger(L, -1)) {
            const auto v = static_cast<int>(lua_tointeger(L, -1));
            if (v > 0) {
                options.maxThreads = v;
            }
        }
        lua_pop(L, 1);
    }

    return options;
}

int HttpServerLuaBindings::luaServerListen(lua_State* L) {
    auto* builder = static_cast<ServerBuilderUserdata*>(luaL_checkudata(L, 1, kServerMeta));

    HttpServerOptions options;
    if (lua_isinteger(L, 2)) {
        options.port = lua_tointeger(L, 2);
    } else if (lua_istable(L, 2)) {
        options = readOptions(L, 2);
    } else {
        options = readOptions(L, 3);
    }

    Runtime& rt = *builder->runtime;

    // duplicate registry ref for native server lifetime; builder userdata may be collected once the chunk returns
    lua_rawgeti(L, LUA_REGISTRYINDEX, builder->handlerRef);
    const int persistedHandlerRef = luaL_ref(L, LUA_REGISTRYINDEX);

    auto handler = [&rt, persistedHandlerRef](const HttpRequest& request, std::shared_ptr<HttpResponse> response) {
        rt.mainLoop().post([&rt, persistedHandlerRef, request, response = std::move(response)] {
            lua_State* mainState = rt.luaState();
            lua_State* thread = lua_newthread(mainState);
            int threadRef = luaL_ref(mainState, LUA_REGISTRYINDEX);

            lua_rawgeti(mainState, LUA_REGISTRYINDEX, persistedHandlerRef);
            lua_xmove(mainState, thread, 1);

            HttpServerLuaBindings::pushRequest(thread, request);
            HttpServerLuaBindings::pushResponse(thread, response);

            int nres = 0;
            int status = lua_resume(thread, mainState, 2, &nres);
            if (status != LUA_OK && status != LUA_YIELD) {
                const char* message = lua_tostring(thread, -1);
                std::string detail = message ? message : "A request handler failed without a message.";
                log::Log::error("http", detail);
                response->setStatus(500);
                response->end("Internal server error.");
                if (nres > 0) {
                    lua_pop(thread, nres);
                }
            } else if (status == LUA_OK) {
                if (!response->ended()) {
                    response->setStatus(204);
                    response->end("");
                }
            }

            // once the handler yields the promise machinery holds its own ref to the coroutine, so release ours in every case.
            luaL_unref(mainState, LUA_REGISTRYINDEX, threadRef);
        });
    };

    auto server = std::shared_ptr<HttpServer>(
        new PocoHttpServer(rt, std::move(options), std::move(handler)),
        [luaMain = rt.luaState(), persistedHandlerRef](HttpServer* p) {
            if (persistedHandlerRef != LUA_NOREF) {
                luaL_unref(luaMain, LUA_REGISTRYINDEX, persistedHandlerRef);
            }
            delete p;
        });
    server->start();
    rt.addServer(server);

    std::ostringstream started;
    started << "Listening on " << (options.tls ? "https" : "http") << "://" << options.host << ":" << options.port << ".";
    log::Log::line("http", started.str());

    return 0;
}

int HttpServerLuaBindings::luaCreateServer(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);

    auto& rt = luaRuntime(L);
    void* memory = lua_newuserdatauv(L, sizeof(ServerBuilderUserdata), 0);
    auto* userdata = new (memory) ServerBuilderUserdata();
    userdata->runtime = &rt;

    lua_pushvalue(L, 1);
    userdata->handlerRef = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_getmetatable(L, kServerMeta);
    lua_setmetatable(L, -2);

    return 1;
}

void HttpServerLuaBindings::createMetatables(lua_State* L) {
    if (luaL_newmetatable(L, kResponseMeta)) {
        lua_pushcfunction(L, &HttpServerLuaBindings::luaResponseGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        lua_pushcfunction(L, &HttpServerLuaBindings::luaResponseStatus);
        lua_setfield(L, -2, "status");
        lua_pushcfunction(L, &HttpServerLuaBindings::luaResponseSetHeader);
        lua_setfield(L, -2, "setHeader");
        lua_pushcfunction(L, &HttpServerLuaBindings::luaResponseFinish);
        lua_setfield(L, -2, "finish");
#if VARN_JSON_DRIVER_NLOHMANN
        lua_pushcfunction(L, &HttpServerLuaBindings::luaResponseJson);
        lua_setfield(L, -2, "json");
#endif
#if VARN_XML_DRIVER_PUGIXML
        lua_pushcfunction(L, &HttpServerLuaBindings::luaResponseXml);
        lua_setfield(L, -2, "xml");
#endif
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, kServerMeta)) {
        lua_pushcfunction(L, &HttpServerLuaBindings::luaServerGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        lua_pushcfunction(L, &HttpServerLuaBindings::luaServerListen);
        lua_setfield(L, -2, "listen");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

int HttpServerLuaBindings::luaOpen(lua_State* L) {
    createMetatables(L);

    lua_newtable(L);
    lua_pushcfunction(L, &HttpServerLuaBindings::luaCreateServer);
    lua_setfield(L, -2, "createServer");
    HttpClientModule::registerClient(L);
    return 1;
}

} // namespace

void HttpServerModule::install(lua_State* L) {
    luaL_requiref(L, "http", &HttpServerLuaBindings::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::http
