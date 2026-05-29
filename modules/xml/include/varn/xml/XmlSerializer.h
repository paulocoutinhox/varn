#pragma once

#include <lua.hpp>
#include <string>

namespace varn::xml {

std::string serializeLuaTable(lua_State* L, int index);

} // namespace varn::xml
