#include "varn/json/JsonSerializer.h"

#include <nlohmann/json.hpp>

#include <lua.hpp>

namespace varn::json {

namespace {

nlohmann::json luaValueToJson(lua_State* L, int index) {
    index = lua_absindex(L, index);
    const int t = lua_type(L, index);
    switch (t) {
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(L, index, &len);
            return s ? nlohmann::json(std::string(s, len)) : nlohmann::json(nullptr);
        }
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) {
                return nlohmann::json(lua_tointeger(L, index));
            }
            return nlohmann::json(lua_tonumber(L, index));
        case LUA_TBOOLEAN:
            return nlohmann::json(lua_toboolean(L, index) != 0);
        case LUA_TNIL:
            return nlohmann::json(nullptr);
        case LUA_TTABLE: {
            nlohmann::json obj = nlohmann::json::object();
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                std::string key = lua_tostring(L, -2) ? lua_tostring(L, -2) : "";
                obj[key] = luaValueToJson(L, -1);
                lua_pop(L, 1);
            }
            return obj;
        }
        default:
            return nlohmann::json("[unsupported]");
    }
}

} // namespace

std::string serializeLuaTable(lua_State* L, int index) {
    if (!lua_istable(L, index)) {
        return "{}";
    }
    nlohmann::json j = luaValueToJson(L, index);
    if (!j.is_object()) {
        nlohmann::json wrap = nlohmann::json::object();
        wrap["value"] = std::move(j);
        return wrap.dump();
    }
    return j.dump();
}

} // namespace varn::json
