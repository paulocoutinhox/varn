#include "varn/json/JsonSerializer.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <lua.hpp>
#include <string>

namespace varn::json {

namespace {

// encodes a lua string as a json string, replacing invalid utf-8 so dump never throws on raw bytes.
std::string dumpString(const std::string& value) {
    return nlohmann::json(value).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// a table is a json array only when its keys are exactly the contiguous integers 1..n.
bool isSequence(lua_State* L, int index, lua_Integer& length) {
    length = static_cast<lua_Integer>(lua_rawlen(L, index));
    if (length <= 0) {
        return false;
    }

    lua_Integer counted = 0;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        ++counted;
        if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 || lua_tointeger(L, -2) > length) {
            lua_pop(L, 2);
            return false;
        }
        lua_pop(L, 1);
    }

    return counted == length;
}

} // namespace

void JsonSerializer::appendValue(lua_State* L, int index, std::string& out, int depth) {
    index = lua_absindex(L, index);
    const int type = lua_type(L, index);
    switch (type) {
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(L, index, &len);
            out += dumpString(s ? std::string(s, len) : std::string());
            return;
        }
        case LUA_TNUMBER:
            if (lua_isinteger(L, index)) {
                out += nlohmann::json(lua_tointeger(L, index)).dump();
                return;
            }
            // json has no representation for nan or infinity, so emit null instead of throwing.
            {
                const double number = lua_tonumber(L, index);
                out += std::isfinite(number) ? nlohmann::json(number).dump() : std::string("null");
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
                out += dumpString("[max depth]");
                return;
            }

            // each nesting level keeps a key and value on the stack, so reserve room before pushing more.
            if (lua_checkstack(L, 4) == 0) {
                out += dumpString("[too deep]");
                return;
            }

            lua_Integer length = 0;
            if (isSequence(L, index, length)) {
                out += '[';
                for (lua_Integer i = 1; i <= length; ++i) {
                    if (i > 1) {
                        out += ',';
                    }
                    lua_rawgeti(L, index, i);
                    appendValue(L, -1, out, depth + 1);
                    lua_pop(L, 1);
                }
                out += ']';
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

                out += dumpString(key);
                out += ':';
                appendValue(L, -1, out, depth + 1);

                lua_pop(L, 1);
            }
            out += '}';
            return;
        }
        default:
            out += dumpString("[unsupported]");
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

std::string JsonSerializer::encode(lua_State* L, int index, int indent) {
    std::string compact;
    compact.reserve(64);
    appendValue(L, index, compact, 0);

    if (indent <= 0) {
        return compact;
    }

    // pretty-print by reparsing the compact form, which preserves the value types we just wrote.
    nlohmann::json parsed = nlohmann::json::parse(compact, nullptr, false);
    if (parsed.is_discarded()) {
        return compact;
    }
    return parsed.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

namespace {

void pushJson(lua_State* L, const nlohmann::json& value, int depth) {
    constexpr int maxDepth = 64;
    if (depth >= maxDepth || lua_checkstack(L, 4) == 0) {
        lua_pushnil(L);
        return;
    }

    if (value.is_object()) {
        lua_createtable(L, 0, static_cast<int>(value.size()));
        for (auto it = value.begin(); it != value.end(); ++it) {
            pushJson(L, it.value(), depth + 1);
            lua_setfield(L, -2, it.key().c_str());
        }
        return;
    }

    if (value.is_array()) {
        lua_createtable(L, static_cast<int>(value.size()), 0);
        int index = 0;
        for (const auto& element : value) {
            pushJson(L, element, depth + 1);
            lua_rawseti(L, -2, ++index);
        }
        return;
    }

    if (value.is_string()) {
        const std::string& text = value.get_ref<const nlohmann::json::string_t&>();
        lua_pushlstring(L, text.data(), text.size());
        return;
    }

    if (value.is_boolean()) {
        lua_pushboolean(L, value.get<bool>() ? 1 : 0);
        return;
    }

    if (value.is_number_unsigned()) {
        const std::uint64_t number = value.get<std::uint64_t>();
        // values past the signed range cannot be a lua integer, so keep them as a number.
        if (number > static_cast<std::uint64_t>(std::numeric_limits<lua_Integer>::max())) {
            lua_pushnumber(L, static_cast<lua_Number>(number));
        } else {
            lua_pushinteger(L, static_cast<lua_Integer>(number));
        }
        return;
    }

    if (value.is_number_integer()) {
        lua_pushinteger(L, value.get<lua_Integer>());
        return;
    }

    if (value.is_number_float()) {
        lua_pushnumber(L, value.get<lua_Number>());
        return;
    }

    lua_pushnil(L);
}

} // namespace

bool JsonSerializer::deserialize(lua_State* L, const std::string& text) {
    // reject pathologically nested input before the recursive parser can overflow the stack.
    constexpr int maxParseDepth = 200;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > maxParseDepth) {
                return false;
            }
        } else if (c == ']' || c == '}') {
            if (depth > 0) {
                --depth;
            }
        }
    }

    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded()) {
        return false;
    }

    pushJson(L, parsed, 0);
    return true;
}

} // namespace varn::json
