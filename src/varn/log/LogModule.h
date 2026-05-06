#pragma once

#include "varn/log/Log.h"

struct lua_State;

namespace varn {

class LogModule {
public:
    LogModule() = delete;

    static void install(struct lua_State* L);

private:
    static void emitAt(struct lua_State* L, log::Level level);

    static int luaDebug(struct lua_State* L);
    static int luaInfo(struct lua_State* L);
    static int luaWarn(struct lua_State* L);
    static int luaError(struct lua_State* L);
    static int luaOpen(struct lua_State* L);
};

} // namespace varn
