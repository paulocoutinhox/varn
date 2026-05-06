#include "varn/async/AsyncModule.h"
#include "varn/async/Promise.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"

#include <chrono>
#include <memory>
#include <thread>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace varn {

Runtime& AsyncModule::luaRuntime(lua_State* L) {
    return *static_cast<Runtime*>(varn::lua::LuaHelpers::getRuntime(L));
}

int AsyncModule::luaSleep(lua_State* L) {
    auto& rt = luaRuntime(L);
    auto ms = static_cast<int>(luaL_checkinteger(L, 1));
    auto promise = std::make_shared<Promise>(rt);

    rt.taskPool().post([promise, ms] {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(ms);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
        promise->resolve("ok");
    });

    Promise::push(L, promise);
    return 1;
}

int AsyncModule::luaSpawn(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);

    lua_State* thread = lua_newthread(L);
    int threadRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pushvalue(L, 1);
    lua_xmove(L, thread, 1);

#if defined(__EMSCRIPTEN__)
    lua_sethook(thread, nullptr, 0, 0);
#endif

    int nres = 0;
    int status = lua_resume(thread, L, 0, &nres);
    if (status != LUA_OK && status != LUA_YIELD) {
        const char* message = lua_tostring(thread, -1);
        luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
        return luaL_error(L, "%s", message ? message : "async.spawn failed");
    }

    if (status == LUA_OK) {
        luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
    }

    return 0;
}

int AsyncModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &AsyncModule::luaSleep);
    lua_setfield(L, -2, "sleep");

    lua_pushcfunction(L, &AsyncModule::luaSpawn);
    lua_setfield(L, -2, "spawn");

    return 1;
}

void AsyncModule::install(lua_State* L) {
    luaL_requiref(L, "async", &AsyncModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn
