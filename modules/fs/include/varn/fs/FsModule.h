#pragma once

struct lua_State;

namespace varn::runtime {
class Runtime;
}

namespace varn::fs {

class FsModule {
public:
    FsModule() = delete;

    static void install(struct lua_State* L);

private:
    static varn::runtime::Runtime& luaRuntime(struct lua_State* L);

    static int luaReadFile(struct lua_State* L);
    static int luaWriteFile(struct lua_State* L);
    static int luaExists(struct lua_State* L);
    static int luaOpen(struct lua_State* L);
};

} // namespace varn::fs
