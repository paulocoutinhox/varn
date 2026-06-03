#include "NlohmannJsonConvert.h"

#include <cstdint>
#include <limits>
#include <string>

namespace varn::json {

// encodes a lua string as a json string, replacing invalid utf-8 so dump never throws on raw bytes.
std::string JsonConvert::dumpString(const std::string& value) {
    return nlohmann::json(value).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

// a table is a json array only when its keys are exactly the contiguous integers 1..n.
bool JsonConvert::isSequence(lua_State* L, int index, lua_Integer& length) {
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

void JsonConvert::pushJson(lua_State* L, const nlohmann::json& value, int depth) {
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

} // namespace varn::json
