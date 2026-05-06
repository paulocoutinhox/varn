#include "varn/platform/PlatformModule.h"

#include "VarnVersion.h"
#include "varn/platform/PlatformInfo.h"

#include <lua.hpp>

#include <string>

namespace varn {

namespace {

int luaOs(lua_State* L) {
    const std::string v = platform::osId();
    lua_pushlstring(L, v.data(), v.size());
    return 1;
}

int luaArch(lua_State* L) {
    const std::string v = platform::archId();
    lua_pushlstring(L, v.data(), v.size());
    return 1;
}

int luaLibPrefix(lua_State* L) {
    const std::string v = platform::libPrefix();
    lua_pushlstring(L, v.data(), v.size());
    return 1;
}

int luaShlibSuffix(lua_State* L) {
    const std::string v = platform::shlibSuffix();
    lua_pushlstring(L, v.data(), v.size());
    return 1;
}

int luaLibraryFilename(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    try {
        const std::string path = platform::libraryFilenameForName(name);
        lua_pushlstring(L, path.data(), path.size());
        return 1;
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }
}

int luaHostVersion(lua_State* L) {
    lua_pushstring(L, VARN_VERSION_STRING);
    return 1;
}

int luaLibraryPathByName(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    std::string subdir = ".";
    if (lua_gettop(L) >= 2 && lua_isstring(L, 2)) {
        subdir = lua_tostring(L, 2);
    }
    try {
        const std::string file = platform::libraryFilenameForName(name);
        std::string out = subdir;
        if (!out.empty() && out.back() != '/' && out.back() != '\\') {
            out += '/';
        }
        out += file;
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    } catch (const std::exception& ex) {
        return luaL_error(L, "%s", ex.what());
    }
}

} // namespace

int PlatformModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &luaOs);
    lua_setfield(L, -2, "os");

    lua_pushcfunction(L, &luaArch);
    lua_setfield(L, -2, "arch");

    lua_pushcfunction(L, &luaHostVersion);
    lua_setfield(L, -2, "hostVersion");

    lua_pushcfunction(L, &luaLibPrefix);
    lua_setfield(L, -2, "libPrefix");

    lua_pushcfunction(L, &luaShlibSuffix);
    lua_setfield(L, -2, "shlibSuffix");

    lua_pushcfunction(L, &luaLibraryFilename);
    lua_setfield(L, -2, "libraryFilename");

    lua_pushcfunction(L, &luaLibraryPathByName);
    lua_setfield(L, -2, "getLibraryPathByName");

    return 1;
}

void PlatformModule::install(lua_State* L) {
    luaL_requiref(L, "platform", &PlatformModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn
