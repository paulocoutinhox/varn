#include "varn/lua/LuaEngine.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/lua/NativeModules.h"
#include "varn/log/Log.h"
#include "varn/runtime/Runtime.h"

namespace varn::lua {

LuaEngine::LuaEngine(varn::runtime::Runtime& runtime) : runtime_(runtime) {
    L_ = luaL_newstate();
    luaL_openlibs(L_);

    varn::lua::LuaHelpers::pushRuntime(L_, &runtime_);
    configureArgTable();
    installNativeModules();
}

LuaEngine::~LuaEngine() {
    if (L_) {
        lua_close(L_);
    }
}

int LuaEngine::runFile(const std::string& path) {
    lua_pushcfunction(L_, &varn::lua::LuaHelpers::traceback);
    int tracebackIndex = lua_gettop(L_);

    if (luaL_loadfile(L_, path.c_str()) != LUA_OK) {
        const char* message = lua_tostring(L_, -1);
        log::Log::error("lua", message ? message : "The script could not be loaded.");
        lua_pop(L_, 2);
        return 1;
    }

    if (lua_pcall(L_, 0, 0, tracebackIndex) != LUA_OK) {
        const char* message = lua_tostring(L_, -1);
        log::Log::error("lua", message ? message : "The script failed to run.");
        lua_pop(L_, 2);
        return 1;
    }

    lua_pop(L_, 1);
    return 0;
}

bool LuaEngine::runStringWithoutEventLoop(const std::string& source, const std::string& chunkName, std::string* errorMessage,
                                          LuaPrePcallHook prePcallHook, void* prePcallUserdata) {
    lua_pushcfunction(L_, &varn::lua::LuaHelpers::traceback);
    const int tracebackIndex = lua_gettop(L_);

    if (luaL_loadbuffer(L_, source.data(), source.size(), chunkName.c_str()) != LUA_OK) {
        if (errorMessage != nullptr) {
            size_t len = 0;
            const char* message = lua_tolstring(L_, -1, &len);
            *errorMessage = message ? std::string(message, len) : std::string("Lua load failed.");
        }
        lua_pop(L_, 2);
        return false;
    }

    if (prePcallHook != nullptr) {
        prePcallHook(L_, prePcallUserdata);
    }

    if (lua_pcall(L_, 0, 0, tracebackIndex) != LUA_OK) {
        if (errorMessage != nullptr) {
            size_t len = 0;
            const char* message = lua_tolstring(L_, -1, &len);
            *errorMessage = message ? std::string(message, len) : std::string("Lua runtime failed.");
        }
        lua_pop(L_, 2);
        return false;
    }

    lua_pop(L_, 1);
    return true;
}

int LuaEngine::runString(const std::string& source, const std::string& chunkName) {
    lua_pushcfunction(L_, &varn::lua::LuaHelpers::traceback);
    int tracebackIndex = lua_gettop(L_);

    if (luaL_loadbuffer(L_, source.data(), source.size(), chunkName.c_str()) != LUA_OK) {
        const char* message = lua_tostring(L_, -1);
        log::Log::error("lua", message ? message : "The source could not be loaded.");
        lua_pop(L_, 2);
        return 1;
    }

    if (lua_pcall(L_, 0, 0, tracebackIndex) != LUA_OK) {
        const char* message = lua_tostring(L_, -1);
        log::Log::error("lua", message ? message : "The source failed to run.");
        lua_pop(L_, 2);
        return 1;
    }

    lua_pop(L_, 1);
    return 0;
}

lua_State* LuaEngine::state() {
    return L_;
}

void LuaEngine::installNativeModules() {
    varn::lua::NativeModuleRegistry::installAll(L_);
}

void LuaEngine::configureArgTable() {
    lua_newtable(L_);

    const auto& args = runtime_.args();
    for (std::size_t i = 0; i < args.size(); ++i) {
        lua_pushlstring(L_, args[i].data(), args[i].size());
        lua_rawseti(L_, -2, static_cast<int>(i));
    }

    lua_setglobal(L_, "arg");
}

} // namespace varn::lua
