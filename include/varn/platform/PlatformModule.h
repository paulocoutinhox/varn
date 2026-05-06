#pragma once

struct lua_State;

namespace varn {

class PlatformModule {
public:
    PlatformModule() = delete;
    static void install(lua_State* L);

private:
    static int luaOpen(lua_State* L);
};

} // namespace varn
