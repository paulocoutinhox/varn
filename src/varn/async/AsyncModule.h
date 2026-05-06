#pragma once

struct lua_State;

namespace varn {

class Runtime;

class AsyncModule {
public:
    AsyncModule() = delete;

    static void install(struct lua_State* L);

private:
    static Runtime& luaRuntime(struct lua_State* L);

    static int luaSleep(struct lua_State* L);
    static int luaSpawn(struct lua_State* L);
    static int luaOpen(struct lua_State* L);
};

} // namespace varn
