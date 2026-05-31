#include "varn/async/AsyncModule.h"
#include "varn/async/Promise.h"
#include "varn/lua/LuaHelpers.h"
#include "varn/runtime/Runtime.h"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace varn::async {

using varn::runtime::Runtime;

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

int AsyncModule::entryContinuation(lua_State* L, int status, lua_KContext ctx) {
    const bool stopLoopOnSuccess = ctx != 0;
    auto& rt = luaRuntime(L);

    if (status != LUA_OK && status != LUA_YIELD) {
        const char* message = lua_tostring(L, -1);
        rt.onAsyncComplete(false, stopLoopOnSuccess, message ? message : "");
        return 0;
    }

    rt.onAsyncComplete(true, stopLoopOnSuccess, std::string());
    return 0;
}

int AsyncModule::entryBody(lua_State* L) {
    const lua_KContext ctx = lua_toboolean(L, lua_upvalueindex(2)) ? 1 : 0;

    // run the user function under a protected call that survives await yields.
    // an uncaught error becomes a single reported outcome instead of being lost inside the resume machinery.
    lua_pushvalue(L, lua_upvalueindex(1));
    const int status = lua_pcallk(L, 0, 0, 0, ctx, &AsyncModule::entryContinuation);
    return entryContinuation(L, status, ctx);
}

int AsyncModule::startEntry(lua_State* L, bool stopLoopOnSuccess) {
    luaL_checktype(L, 1, LUA_TFUNCTION);

    lua_State* thread = lua_newthread(L);
    const int threadRef = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pushvalue(L, 1);
    lua_xmove(L, thread, 1);
    lua_pushboolean(thread, stopLoopOnSuccess ? 1 : 0);
    lua_pushcclosure(thread, &AsyncModule::entryBody, 2);

#if defined(__EMSCRIPTEN__)
    lua_sethook(thread, nullptr, 0, 0);
#endif

    int nres = 0;
    const int status = lua_resume(thread, L, 0, &nres);
    if (status != LUA_OK && status != LUA_YIELD) {
        const char* message = lua_tostring(thread, -1);
        luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
        return luaL_error(L, "%s", message ? message : "[async] The task could not be started.");
    }

    // once the entry yields the promise machinery owns the coroutine, so release our ref in every case.
    luaL_unref(L, LUA_REGISTRYINDEX, threadRef);
    return 0;
}

int AsyncModule::luaSpawn(lua_State* L) {
    return startEntry(L, false);
}

int AsyncModule::luaRun(lua_State* L) {
    return startEntry(L, true);
}

int AsyncModule::luaOpen(lua_State* L) {
    lua_newtable(L);

    lua_pushcfunction(L, &AsyncModule::luaSleep);
    lua_setfield(L, -2, "sleep");

    lua_pushcfunction(L, &AsyncModule::luaSpawn);
    lua_setfield(L, -2, "spawn");

    lua_pushcfunction(L, &AsyncModule::luaRun);
    lua_setfield(L, -2, "run");

    return 1;
}

void AsyncModule::install(lua_State* L) {
    luaL_requiref(L, "async", &AsyncModule::luaOpen, 1);
    lua_pop(L, 1);
}

} // namespace varn::async
