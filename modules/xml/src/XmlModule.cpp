#include "varn/xml/XmlModule.h"
#include "varn/xml/XmlSerializer.h"

#include <lua.hpp>

#include <exception>
#include <stdexcept>
#include <string>

namespace varn::xml {

// reads the indent width from the options table: an explicit indent, or 2 when pretty is set.
int XmlModule::readIndent(lua_State* L, int optsIndex) {
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

// push the error message inside the catch block, then call lua_error from a frame with no live c++
// destructors. on msvc, a lua_error longjmp over a std::string would trip the /gs stack canary.
int XmlModule::luaEncode(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const int indent = readIndent(L, 2);

    try {
        const std::string out = XmlSerializer::encodeNode(L, 1, indent);
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    } catch (const std::exception& ex) {
        lua_pushstring(L, ex.what());
    }
    return lua_error(L);
}

int XmlModule::luaDecode(lua_State* L) {
    std::size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);

    try {
        if (XmlSerializer::parse(L, std::string(text, length))) {
            return 1;
        }
        throw std::runtime_error("[XmlModule] The input is not valid XML.");
    } catch (const std::exception& ex) {
        lua_pushstring(L, ex.what());
    }
    return lua_error(L);
}

int XmlModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &XmlModule::luaEncode);
    lua_setfield(L, -2, "encode");
    lua_pushcfunction(L, &XmlModule::luaEncode);
    lua_setfield(L, -2, "stringify");

    lua_pushcfunction(L, &XmlModule::luaDecode);
    lua_setfield(L, -2, "decode");
    lua_pushcfunction(L, &XmlModule::luaDecode);
    lua_setfield(L, -2, "parse");

    return 1;
}

void XmlModule::install(lua_State* L) {
    luaL_requiref(L, "xml", &XmlModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::xml
