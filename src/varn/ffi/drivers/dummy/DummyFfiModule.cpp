#include <lua.hpp>

namespace {

int ffiDisabled(lua_State* L) {
    return luaL_error(
        L,
        "FFI is not available in this build (VARN_FFI_DRIVER=" VARN_FFI_DRIVER_NAME "). "
        "On supported hosts install Sourceware libffi (https://sourceware.org/libffi/) and pkg-config, "
        "then configure with -DVARN_FFI_DRIVER=LIBFFI. Cross-compiled and WebAssembly builds use "
        "DUMMY by default; see docs/build.md.");
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
