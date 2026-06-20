#include "varn/json/JsonSerializer.h"

#include "NlohmannJsonConvert.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <lua.hpp>
#include <string>

namespace varn::json
{

void JsonSerializer::appendValue(lua_State* L, int index, std::string& out, int depth)
{
    index = lua_absindex(L, index);
    const int type = lua_type(L, index);
    switch (type)
    {
    case LUA_TSTRING:
    {
        size_t len = 0;
        const char* s = lua_tolstring(L, index, &len);
        out += JsonConvert::dumpString(s ? std::string(s, len) : std::string());
        return;
    }
    case LUA_TNUMBER:
        if (lua_isinteger(L, index))
        {
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
    case LUA_TTABLE:
    {
        // matches the decode side (parse/convert caps) so a value that round-trips in can round-trip out, acting as a stack-overflow guard rather than a usage limit so it sits well above real data.
        constexpr int maxDepth = 256;
        if (depth >= maxDepth)
        {
            out += JsonConvert::dumpString("[max depth]");
            return;
        }

        // each nesting level keeps a key and value on the stack, so reserve room before pushing more.
        if (lua_checkstack(L, 4) == 0)
        {
            out += JsonConvert::dumpString("[too deep]");
            return;
        }

        lua_Integer length = 0;
        if (JsonConvert::isSequence(L, index, length))
        {
            out += '[';
            for (lua_Integer i = 1; i <= length; ++i)
            {
                if (i > 1)
                {
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
        while (lua_next(L, index) != 0)
        {
            // coerce a copy of the key so lua_next still sees the original on the next step.
            lua_pushvalue(L, -2);
            const char* raw = lua_tolstring(L, -1, nullptr);
            std::string key = raw ? raw : "";
            lua_pop(L, 1);

            if (count > 0)
            {
                out += ',';
            }
            ++count;

            out += JsonConvert::dumpString(key);
            out += ':';
            appendValue(L, -1, out, depth + 1);

            lua_pop(L, 1);
        }
        out += '}';
        return;
    }
    default:
        out += JsonConvert::dumpString("[unsupported]");
        return;
    }
}

std::string JsonSerializer::serialize(lua_State* L, int index)
{
    if (!lua_istable(L, index))
    {
        return "{}";
    }

    std::string out;
    out.reserve(64);
    appendValue(L, index, out, 0);
    return out;
}

std::string JsonSerializer::encode(lua_State* L, int index, int indent)
{
    std::string compact;
    compact.reserve(64);
    appendValue(L, index, compact, 0);

    if (indent <= 0)
    {
        return compact;
    }

    // pretty-print by reparsing the compact form, which preserves the value types we just wrote.
    nlohmann::json parsed = nlohmann::json::parse(compact, nullptr, false);
    if (parsed.is_discarded())
    {
        return compact;
    }
    return parsed.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

bool JsonSerializer::deserialize(lua_State* L, const std::string& text)
{
    // reject pathologically nested input before the recursive parser can overflow the stack, kept below JsonConvert::pushJson's depth cap so any accepted document converts fully and is never silently truncated to nil.
    constexpr int maxParseDepth = 200;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (char c : text)
    {
        if (inString)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (c == '\\')
            {
                escaped = true;
            }
            else if (c == '"')
            {
                inString = false;
            }
            continue;
        }
        if (c == '"')
        {
            inString = true;
        }
        else if (c == '[' || c == '{')
        {
            if (++depth > maxParseDepth)
            {
                return false;
            }
        }
        else if (c == ']' || c == '}')
        {
            if (depth > 0)
            {
                --depth;
            }
        }
    }

    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded())
    {
        return false;
    }

    JsonConvert::pushJson(L, parsed, 0);
    return true;
}

} // namespace varn::json
