#include "varn/json/JsonModule.h"
#include "varn/json/JsonSerializer.h"

#include <lua.hpp>

#include <exception>
#include <stdexcept>
#include <string>

#if defined(_MSC_VER)
#define VARN_NOINLINE __declspec(noinline)
#else
#define VARN_NOINLINE __attribute__((noinline))
#endif

namespace varn::json {

// reads the indent width from the options table: an explicit indent, or 2 when pretty is set.
int JsonModule::readIndent(lua_State* L, int optsIndex) {
    if (!lua_istable(L, optsIndex)) {
        return 0;
    }

    int indent = 0;
    lua_getfield(L, optsIndex, "indent");
    if (lua_isinteger(L, -1)) {
        const int value = static_cast<int>(lua_tointeger(L, -1));
        indent = value > 0 ? value : 0;
    }
    lua_pop(L, 1);

    lua_getfield(L, optsIndex, "pretty");
    if (lua_toboolean(L, -1) && indent <= 0) {
        indent = 2;
    }
    lua_pop(L, 1);

    return indent;
}

// the worker holds the try/catch in its own frame so the entry function below stays free of c++ eh
// metadata and live destructors. msvc's /gs canary aborts the process when lua_error longjmps across
// a try/catch that ran in the same frame, so the worker returns -1 with the message on top of the
// lua stack and the entry function calls lua_error from a frame containing only pod locals.

VARN_NOINLINE int JsonModule::luaEncodeWorker(lua_State* L) {
    const int indent = readIndent(L, 2);
    try {
        const std::string out = JsonSerializer::encode(L, 1, indent);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    } catch (const std::exception& ex) {
        lua_pushstring(L, ex.what());
    }
    return -1;
}

int JsonModule::luaEncode(lua_State* L) {
    const int n = luaEncodeWorker(L);
    if (n < 0) {
        return lua_error(L);
    }
    return n;
}

VARN_NOINLINE int JsonModule::luaDecodeWorker(lua_State* L) {
    std::size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    try {
        if (JsonSerializer::deserialize(L, std::string(text, length))) {
            return 1;
        }
        lua_pushliteral(L, "[JsonModule] The input is not valid JSON.");
    } catch (const std::exception& ex) {
        lua_pushstring(L, ex.what());
    }
    return -1;
}

int JsonModule::luaDecode(lua_State* L) {
    const int n = luaDecodeWorker(L);
    if (n < 0) {
        return lua_error(L);
    }
    return n;
}

int JsonModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &JsonModule::luaEncode);
    lua_setfield(L, -2, "encode");
    lua_pushcfunction(L, &JsonModule::luaEncode);
    lua_setfield(L, -2, "stringify");

    lua_pushcfunction(L, &JsonModule::luaDecode);
    lua_setfield(L, -2, "decode");
    lua_pushcfunction(L, &JsonModule::luaDecode);
    lua_setfield(L, -2, "parse");

    return 1;
}

void JsonModule::install(lua_State* L) {
    luaL_requiref(L, "json", &JsonModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::json
