#pragma once

#include <lua.hpp>
#include <string>

namespace varn::json {

class JsonSerializer {
public:
    JsonSerializer() = delete;

    static std::string serialize(lua_State* L, int index);

private:
    static void appendValue(lua_State* L, int index, std::string& out, int depth);
};

} // namespace varn::json
