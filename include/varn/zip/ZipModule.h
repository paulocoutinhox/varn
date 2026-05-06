#pragma once

struct lua_State;

namespace varn {

class ZipModule {
public:
    ZipModule() = delete;
    static void install(lua_State* L);

private:
    static int luaOpen(lua_State* L);
    static int luaExtract(lua_State* L);
    static int luaCreate(lua_State* L);
    static int luaList(lua_State* L);
};

} // namespace varn
