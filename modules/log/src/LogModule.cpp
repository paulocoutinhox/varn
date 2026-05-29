#include "varn/log/LogModule.h"

#include "varn/log/Log.h"

#include <lua.hpp>
#include <string>

namespace varn::log {

void LogModule::emitAt(lua_State* L, Level level) {
    const int n = lua_gettop(L);
    if (n == 0) {
        return;
    }

    // each stack slot is coerced in place so avoid popping between arguments
    std::string combined;
    combined.reserve(64);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) {
            combined += '\t';
        }
        size_t len = 0;
        const char* chunk = luaL_tolstring(L, i, &len);
        if (chunk != nullptr) {
            combined.append(chunk, len);
        }
    }

    Log::emit(level, combined);
}

int LogModule::luaDebug(lua_State* L) {
    emitAt(L, Level::Debug);
    return 0;
}

int LogModule::luaInfo(lua_State* L) {
    emitAt(L, Level::Info);
    return 0;
}

int LogModule::luaWarn(lua_State* L) {
    emitAt(L, Level::Warn);
    return 0;
}

int LogModule::luaError(lua_State* L) {
    emitAt(L, Level::Error);
    return 0;
}

int LogModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &LogModule::luaDebug);
    lua_setfield(L, -2, "debug");

    lua_pushcfunction(L, &LogModule::luaInfo);
    lua_setfield(L, -2, "info");

    lua_pushcfunction(L, &LogModule::luaWarn);
    lua_setfield(L, -2, "warn");

    lua_pushcfunction(L, &LogModule::luaError);
    lua_setfield(L, -2, "error");

    return 1;
}

void LogModule::install(lua_State* L) {
    luaL_requiref(L, "log", &LogModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::log
