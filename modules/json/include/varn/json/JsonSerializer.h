#pragma once

#include <lua.hpp>
#include <string>

namespace varn::json {

class JsonSerializer {
public:
    JsonSerializer() = delete;

    static std::string serialize(lua_State* L, int index);
    static bool deserialize(lua_State* L, const std::string& text);

    // encodes any lua value (not only a table); indent > 0 pretty-prints with that many spaces.
    static std::string encode(lua_State* L, int index, int indent);

private:
    static void appendValue(lua_State* L, int index, std::string& out, int depth);
};

} // namespace varn::json
