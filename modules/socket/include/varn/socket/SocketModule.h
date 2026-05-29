#pragma once

#include <memory>

struct lua_State;

namespace varn::runtime {
class Runtime;
}

namespace varn::socket {

class TcpConnection;
class TcpListener;

class SocketModule {
public:
    SocketModule() = delete;

    static void install(lua_State* L);

private:
    static varn::runtime::Runtime& luaRuntime(lua_State* L);

    static void pushTcpSocket(lua_State* L, std::shared_ptr<TcpConnection> conn);
    static void pushTcpListener(lua_State* L, std::shared_ptr<TcpListener> listener);

    static std::shared_ptr<TcpConnection>* checkTcpSocket(lua_State* L, int index);
    static std::shared_ptr<TcpListener>* checkTcpListener(lua_State* L, int index);

    static void createMetatables(lua_State* L);

    static int luaTcpSocketGc(lua_State* L);
    static int luaTcpListenerGc(lua_State* L);
    static int luaTcpConnect(lua_State* L);
    static int luaTcpListen(lua_State* L);
    static int luaTcpSocketSend(lua_State* L);
    static int luaTcpSocketReceive(lua_State* L);
    static int luaTcpSocketClose(lua_State* L);
    static int luaTcpListenerAccept(lua_State* L);
    static int luaTcpListenerClose(lua_State* L);
    static int luaOpen(lua_State* L);
};

} // namespace varn::socket
