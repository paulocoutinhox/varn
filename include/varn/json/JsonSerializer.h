#pragma once

#include <lua.hpp>
#include <string>

namespace varn::json {

std::string serializeLuaTable(lua_State* L, int index);

} // namespace varn::json
