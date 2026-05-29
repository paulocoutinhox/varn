#include <lua.hpp>

namespace {

int ffiDisabled(lua_State* L) {
    return luaL_error(L, "[ffi] The FFI module is not available in this build.");
}

constexpr luaL_Reg kFfiLib[] = {
    {"cdef", ffiDisabled},
    {"load", ffiDisabled},
    {"new", ffiDisabled},
    {"cast", ffiDisabled},
    {"metatype", ffiDisabled},
    {"typeof", ffiDisabled},
    {"addressof", ffiDisabled},
    {"gc", ffiDisabled},
    {"sizeof", ffiDisabled},
    {"offsetof", ffiDisabled},
    {"istype", ffiDisabled},
    {"tonumber", ffiDisabled},
    {"string", ffiDisabled},
    {"copy", ffiDisabled},
    {"fill", ffiDisabled},
    {"errno", ffiDisabled},
    {nullptr, nullptr},
};

} // namespace

extern "C" int luaopen_ffi_dummy(lua_State* L) {
    luaL_newlib(L, kFfiLib);
    lua_pushliteral(L, "0-dummy");
    lua_setfield(L, -2, "VERSION");
    lua_pushnil(L);
    lua_setfield(L, -2, "nullptr");
    lua_newtable(L);
    lua_setfield(L, -2, "C");
    return 1;
}
