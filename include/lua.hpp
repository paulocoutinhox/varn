#pragma once

// C++ convenience header for the Lua C API; upstream Lua sources do not ship lua.hpp.
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
