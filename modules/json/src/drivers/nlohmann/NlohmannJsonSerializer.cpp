#include "varn/json/JsonSerializer.h"

#include <nlohmann/json.hpp>

#include <lua.hpp>
#include <string>

namespace varn::json {

void JsonSerializer::appendValue(lua_State* L, int index, std::string& out, int depth) {
    index = lua_absindex(L, index);
    const int type = lua_type(L, index);
    switch (type) {
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(L, index, &len);
            out += nlohmann::json(s ? std::string(s, len) : std::string()).dump();
            return;
        }
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) {
                out += nlohmann::json(lua_tointeger(L, index)).dump();
            } else {
                out += nlohmann::json(lua_tonumber(L, index)).dump();
            }
            return;
        case LUA_TBOOLEAN:
            out += lua_toboolean(L, index) != 0 ? "true" : "false";
            return;
        case LUA_TNIL:
            out += "null";
            return;
        case LUA_TTABLE: {
            constexpr int maxDepth = 64;
            if (depth >= maxDepth) {
                out += nlohmann::json("[max depth]").dump();
                return;
            }

            // each nesting level keeps a key and value on the stack, so reserve room before pushing more.
            if (lua_checkstack(L, 4) == 0) {
                out += nlohmann::json("[too deep]").dump();
                return;
            }

            out += '{';
            int count = 0;
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                // coerce a copy of the key so lua_next still sees the original on the next step.
                lua_pushvalue(L, -2);
                const char* raw = lua_tolstring(L, -1, nullptr);
                std::string key = raw ? raw : "";
                lua_pop(L, 1);

                if (count > 0) {
                    out += ',';
                }
                ++count;

                out += nlohmann::json(key).dump();
                out += ':';
                appendValue(L, -1, out, depth + 1);

                lua_pop(L, 1);
            }
            out += '}';
            return;
        }
        default:
            out += nlohmann::json("[unsupported]").dump();
            return;
    }
}

std::string JsonSerializer::serialize(lua_State* L, int index) {
    if (!lua_istable(L, index)) {
        return "{}";
    }

    std::string out;
    out.reserve(64);
    appendValue(L, index, out, 0);
    return out;
}

} // namespace varn::json
