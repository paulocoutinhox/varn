#pragma once

// C++ convenience header for the Lua API; upstream Lua sources do not ship lua.hpp.
// lua is vendored and built as C++ (see cmake/dependencies.cmake), so its API has C++ linkage and
// is included without an extern "C" wrapper. building lua as C++ makes a raised lua error unwind
// through the embedding C++ frames as an exception instead of a longjmp, which on MSVC corrupts
// those frames.
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
