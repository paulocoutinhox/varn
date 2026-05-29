#include "varn/runtime/Runtime.h"
#include "varn/wasm/WasmAsyncHost.h"

#include <lua.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#endif

namespace varn::wasm {

#if defined(__EMSCRIPTEN__)
int g_hook_tick = 0;

namespace {

void pumpRuntimeDeferredWork(varn::runtime::Runtime& rt) {
    const double deadlineMs = emscripten_get_now() + 120000.0;
    while (emscripten_get_now() < deadlineMs) {
        bool progressed;
        do {
            progressed = false;
            if (rt.taskPool().hasPostedJobs()) {
                rt.taskPool().drainPostedJobs();
                progressed = true;
            }
            if (rt.mainLoop().hasPendingJobs()) {
                rt.mainLoop().drainPostedJobs();
                progressed = true;
            }
        } while (progressed);

        if (varn::wasm::httpClientFetchInflight().load(std::memory_order_acquire) > 0) {
            emscripten_sleep(0);
            continue;
        }
        break;
    }
}

} // namespace
#endif

namespace detail {

std::string* g_print_sink = nullptr;

struct PrintSinkScope {
    explicit PrintSinkScope(std::string* sink) { g_print_sink = sink; }
    PrintSinkScope(const PrintSinkScope&) = delete;
    PrintSinkScope& operator=(const PrintSinkScope&) = delete;
    ~PrintSinkScope() { g_print_sink = nullptr; }
};

int lua_print_capture(lua_State* L) {
    if (!g_print_sink) {
        return 0;
    }

    int n = lua_gettop(L);
    std::string line;

    for (int i = 1; i <= n; ++i) {
        if (i > 1) {
            line += '\t';
        }
        size_t len = 0;
        const char* chunk = luaL_tolstring(L, i, &len);
        line.append(chunk, len);
        lua_pop(L, 1);
    }

    line += '\n';
    g_print_sink->append(line);
    return 0;
}

#if defined(__EMSCRIPTEN__)
void line_hook(lua_State*, lua_Debug*) {
    ++g_hook_tick;
    if ((g_hook_tick % 32) == 0) {
        emscripten_sleep(0);
    }
}

void wasmAttachLineHook(lua_State* L, void*) {
    g_hook_tick = 0;
    lua_sethook(L, line_hook, LUA_MASKCOUNT, 500);
}
#endif

std::unique_ptr<varn::runtime::Runtime>& wasmRuntime() {
    static std::unique_ptr<varn::runtime::Runtime> instance;
    return instance;
}

} // namespace detail

struct RunResult {
    bool ok = false;
    std::string output;
    std::string error;
};

RunResult run_chunk_impl(const std::string& source) {
    RunResult result;

    auto& rtPtr = detail::wasmRuntime();
    if (!rtPtr) {
        rtPtr = std::make_unique<varn::runtime::Runtime>(std::vector<std::string>{});
        lua_State* Lsetup = rtPtr->luaState();
        lua_pushcfunction(Lsetup, detail::lua_print_capture);
        lua_setglobal(Lsetup, "print");
    }

    varn::runtime::Runtime& rt = *rtPtr;
    lua_State* L = rt.luaState();

    std::string collected;
    std::string err;
    bool ok = false;

    {
        detail::PrintSinkScope sink_scope(&collected);
#if defined(__EMSCRIPTEN__)
        ok = rt.runStringWithoutEventLoop(source, "=wasm", &err, detail::wasmAttachLineHook, nullptr);
        lua_sethook(L, nullptr, 0, 0);

        pumpRuntimeDeferredWork(rt);
#else
        ok = rt.runStringWithoutEventLoop(source, "=wasm", &err);
#endif
    }

    result.output = std::move(collected);
    result.ok = ok;
    if (!ok) {
        result.error = std::move(err);
    }
    return result;
}

} // namespace varn::wasm

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_BINDINGS(varn_wasm) {
    emscripten::value_object<varn::wasm::RunResult>("RunResult")
        .field("ok", &varn::wasm::RunResult::ok)
        .field("output", &varn::wasm::RunResult::output)
        .field("error", &varn::wasm::RunResult::error);

    emscripten::function("varnRunChunk", &varn::wasm::run_chunk_impl);
}
#endif
