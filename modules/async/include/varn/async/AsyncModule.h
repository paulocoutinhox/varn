#pragma once

#include <lua.hpp>

namespace varn::runtime
{
class Runtime;
}

namespace varn::async
{

class AsyncModule
{
public:
    AsyncModule() = delete;

    static void install(lua_State* L);

private:
    static varn::runtime::Runtime& luaRuntime(lua_State* L);

    static int startEntry(lua_State* L, bool stopLoopOnSuccess);
    static int entryBody(lua_State* L);
    static int entryContinuation(lua_State* L, int status, lua_KContext ctx);

    static int luaSleep(lua_State* L);
    static int luaSpawn(lua_State* L);
    static int luaRun(lua_State* L);
    static int luaOpen(lua_State* L);
};

} // namespace varn::async
