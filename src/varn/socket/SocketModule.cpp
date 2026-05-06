#include "varn/socket/SocketModule.h"

#include "varn/async/Promise.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"
#include "varn/socket/SocketTransport.h"

#include <lua.hpp>

#include <memory>
#include <string>
#include <utility>

namespace varn {

namespace {

constexpr const char* kTcpSocketMeta = "varn.TcpSocket";
constexpr const char* kTcpListenerMeta = "varn.TcpListener";

Runtime& luaRuntime(lua_State* L) {
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

void pushTcpSocket(lua_State* L, std::shared_ptr<socket::TcpConnection> conn) {
    void* memory = lua_newuserdatauv(L, sizeof(std::shared_ptr<socket::TcpConnection>), 0);
    new (memory) std::shared_ptr<socket::TcpConnection>(std::move(conn));
    luaL_getmetatable(L, kTcpSocketMeta);
    lua_setmetatable(L, -2);
}

void pushTcpListener(lua_State* L, std::shared_ptr<socket::TcpListener> listener) {
    void* memory = lua_newuserdatauv(L, sizeof(std::shared_ptr<socket::TcpListener>), 0);
    new (memory) std::shared_ptr<socket::TcpListener>(std::move(listener));
    luaL_getmetatable(L, kTcpListenerMeta);
    lua_setmetatable(L, -2);
    luaRuntime(L).retainBackgroundDriver();
}

std::shared_ptr<socket::TcpConnection>* checkTcpSocket(lua_State* L, int index) {
    return static_cast<std::shared_ptr<socket::TcpConnection>*>(luaL_checkudata(L, index, kTcpSocketMeta));
}

std::shared_ptr<socket::TcpListener>* checkTcpListener(lua_State* L, int index) {
    return static_cast<std::shared_ptr<socket::TcpListener>*>(luaL_checkudata(L, index, kTcpListenerMeta));
}

int luaTcpSocketGc(lua_State* L) {
    auto* holder = static_cast<std::shared_ptr<socket::TcpConnection>*>(lua_touserdata(L, 1));
    if (holder != nullptr) {
        if (*holder) {
            try {
                (*holder)->closeBlocking();
            } catch (...) {
            }
        }
        std::destroy_at(holder);
    }
    return 0;
}

int luaTcpListenerGc(lua_State* L) {
    auto* holder = static_cast<std::shared_ptr<socket::TcpListener>*>(lua_touserdata(L, 1));
    if (holder != nullptr) {
        if (*holder) {
            try {
                (*holder)->closeBlocking();
            } catch (...) {
            }
        }
        std::destroy_at(holder);
        luaRuntime(L).releaseBackgroundDriver();
    }
    return 0;
}

int luaTcpConnect(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    const int port = static_cast<int>(luaL_checkinteger(L, 2));
    if (port <= 0 || port > 65535) {
        return luaL_error(L, "socket.tcp.connect: port must be between 1 and 65535");
    }

    auto& rt = luaRuntime(L);
    std::string hostStr = host;
    auto promise = std::make_shared<Promise>(rt);

    rt.taskPool().post([promise, hostStr, port] {
        try {
            auto conn = socket::connectBlocking(hostStr, port);
            promise->resolveCustom([conn](lua_State* lua) { pushTcpSocket(lua, conn); });
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        }
    });

    Promise::push(L, promise);
    return 1;
}

int luaTcpListen(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    const int port = static_cast<int>(luaL_checkinteger(L, 2));
    const int backlog = static_cast<int>(luaL_optinteger(L, 3, 64));
    if (port <= 0 || port > 65535) {
        return luaL_error(L, "socket.tcp.listen: port must be between 1 and 65535");
    }
    if (backlog <= 0 || backlog > 4096) {
        return luaL_error(L, "socket.tcp.listen: backlog must be between 1 and 4096");
    }

    auto& rt = luaRuntime(L);
    std::string hostStr = host;
    auto promise = std::make_shared<Promise>(rt);

    rt.taskPool().post([promise, hostStr, port, backlog] {
        try {
            auto listener = socket::listenBlocking(hostStr, port, backlog);
            promise->resolveCustom([listener](lua_State* lua) { pushTcpListener(lua, listener); });
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        }
    });

    Promise::push(L, promise);
    return 1;
}

int luaTcpSocketSend(lua_State* L) {
    auto* holder = checkTcpSocket(L, 1);
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    std::string payload(data, len);

    auto& rt = luaRuntime(L);
    auto conn = *holder;
    auto promise = std::make_shared<Promise>(rt);

    rt.taskPool().post([promise, conn, payload = std::move(payload)] {
        try {
            conn->sendBlocking(payload);
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        }
    });

    Promise::push(L, promise);
    return 1;
}

int luaTcpSocketReceive(lua_State* L) {
    auto* holder = checkTcpSocket(L, 1);
    const int maxBytes = static_cast<int>(luaL_optinteger(L, 2, 65536));
    if (maxBytes <= 0) {
        return luaL_error(L, "socket:receive: maxBytes must be positive");
    }

    auto& rt = luaRuntime(L);
    auto conn = *holder;
    auto promise = std::make_shared<Promise>(rt);

    rt.taskPool().post([promise, conn, maxBytes] {
        try {
            promise->resolve(conn->receiveBlocking(maxBytes));
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        }
    });

    Promise::push(L, promise);
    return 1;
}

int luaTcpSocketClose(lua_State* L) {
    auto* holder = checkTcpSocket(L, 1);
    auto& rt = luaRuntime(L);
    auto conn = *holder;
    auto promise = std::make_shared<Promise>(rt);

    rt.taskPool().post([promise, conn] {
        try {
            conn->closeBlocking();
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        }
    });

    Promise::push(L, promise);
    return 1;
}

int luaTcpListenerAccept(lua_State* L) {
    auto* holder = checkTcpListener(L, 1);
    auto& rt = luaRuntime(L);
    auto listener = *holder;
    auto promise = std::make_shared<Promise>(rt);

    rt.taskPool().post([promise, listener] {
        try {
            auto conn = listener->acceptBlocking();
            promise->resolveCustom([conn](lua_State* lua) { pushTcpSocket(lua, conn); });
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        }
    });

    Promise::push(L, promise);
    return 1;
}

int luaTcpListenerClose(lua_State* L) {
    auto* holder = checkTcpListener(L, 1);
    auto& rt = luaRuntime(L);
    auto listener = *holder;
    auto promise = std::make_shared<Promise>(rt);

    rt.taskPool().post([promise, listener] {
        try {
            listener->closeBlocking();
            promise->resolve("ok");
        } catch (const std::exception& ex) {
            promise->reject(ex.what());
        }
    });

    Promise::push(L, promise);
    return 1;
}

void createMetatables(lua_State* L) {
    if (luaL_newmetatable(L, kTcpSocketMeta)) {
        lua_pushcfunction(L, &luaTcpSocketGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        lua_pushcfunction(L, &luaTcpSocketSend);
        lua_setfield(L, -2, "send");
        lua_pushcfunction(L, &luaTcpSocketReceive);
        lua_setfield(L, -2, "receive");
        lua_pushcfunction(L, &luaTcpSocketClose);
        lua_setfield(L, -2, "close");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, kTcpListenerMeta)) {
        lua_pushcfunction(L, &luaTcpListenerGc);
        lua_setfield(L, -2, "__gc");

        lua_newtable(L);
        lua_pushcfunction(L, &luaTcpListenerAccept);
        lua_setfield(L, -2, "accept");
        lua_pushcfunction(L, &luaTcpListenerClose);
        lua_setfield(L, -2, "close");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
}

int luaOpen(lua_State* L) {
    createMetatables(L);

    lua_newtable(L);
    lua_newtable(L);
    lua_pushcfunction(L, &luaTcpConnect);
    lua_setfield(L, -2, "connect");
    lua_pushcfunction(L, &luaTcpListen);
    lua_setfield(L, -2, "listen");
    lua_setfield(L, -2, "tcp");
    return 1;
}

} // namespace

void SocketModule::install(lua_State* L) {
    luaL_requiref(L, "socket", &luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn
