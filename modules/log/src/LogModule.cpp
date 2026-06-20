#include "varn/log/LogModule.h"

#include "varn/log/Log.h"

#include <lua.hpp>
#include <string>

namespace varn::log
{

void LogModule::emitAt(lua_State* L, Level level)
{
    const int n = lua_gettop(L);
    if (n == 0)
    {
        return;
    }

    std::string combined;
    combined.reserve(64);
    for (int i = 1; i <= n; ++i)
    {
        if (i > 1)
        {
            combined += '\t';
        }
        appendValue(L, i, combined, 0, false);
    }

    Log::emit(level, combined);
}

void LogModule::appendValue(lua_State* L, int index, std::string& out, int depth, bool quoteStrings)
{
    index = lua_absindex(L, index);

    if (lua_type(L, index) == LUA_TTABLE)
    {
        appendTable(L, index, out, depth);
        return;
    }

    if (quoteStrings && lua_type(L, index) == LUA_TSTRING)
    {
        size_t len = 0;
        const char* text = lua_tolstring(L, index, &len);
        out += '"';
        out.append(text, len);
        out += '"';
        return;
    }

    // luaL_tolstring pushes the coerced text, so pop it once appended.
    size_t len = 0;
    const char* text = luaL_tolstring(L, index, &len);
    out.append(text, len);
    lua_pop(L, 1);
}

void LogModule::appendKey(lua_State* L, int index, std::string& out)
{
    index = lua_absindex(L, index);

    if (lua_type(L, index) == LUA_TSTRING)
    {
        size_t len = 0;
        const char* key = lua_tolstring(L, index, &len);
        out.append(key, len);
        return;
    }

    // coerce a copy so lua_next still sees the original key on the next step.
    out += '[';
    lua_pushvalue(L, index);
    size_t len = 0;
    const char* key = luaL_tolstring(L, -1, &len);
    out.append(key, len);
    lua_pop(L, 2);
    out += ']';
}

void LogModule::appendTable(lua_State* L, int index, std::string& out, int depth)
{
    constexpr int maxDepth = 4;
    constexpr int maxEntries = 32;

    if (depth >= maxDepth)
    {
        out += "{...}";
        return;
    }
    index = lua_absindex(L, index);

    out += '{';
    int count = 0;

    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (count >= maxEntries)
        {
            out += " ...";
            lua_pop(L, 2);
            break;
        }
        out += (count == 0) ? " " : ", ";
        ++count;

        appendKey(L, -2, out);
        out += " = ";
        appendValue(L, -1, out, depth + 1, true);

        lua_pop(L, 1);
    }

    out += (count == 0) ? "}" : " }";
}

int LogModule::luaDebug(lua_State* L)
{
    emitAt(L, Level::Debug);
    return 0;
}

int LogModule::luaInfo(lua_State* L)
{
    emitAt(L, Level::Info);
    return 0;
}

int LogModule::luaWarn(lua_State* L)
{
    emitAt(L, Level::Warn);
    return 0;
}

int LogModule::luaError(lua_State* L)
{
    emitAt(L, Level::Error);
    return 0;
}

int LogModule::luaOpen(lua_State* L)
{
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

void LogModule::install(lua_State* L)
{
    luaL_requiref(L, "log", &LogModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::log
