# Native modules (C++)

This is a short guide to adding a built-in C++ module that exposes functions to Lua.

## The shape of a module

A module is a C++ class with a `static void install(lua_State* L)` method and `private static int lua*(lua_State* L)` callbacks for each Lua function. Keep small helpers as private static methods on the class so the module stays one cohesive type. Avoid naming a method `register` (it is a reserved word in older C++).

```cpp
class ExampleModule {
public:
    static void install(lua_State* L);

private:
    static int luaAdd(lua_State* L) {
        int a = static_cast<int>(luaL_checkinteger(L, 1));
        int b = static_cast<int>(luaL_checkinteger(L, 2));
        lua_pushinteger(L, a + b);
        return 1;
    }
    static int luaOpen(lua_State* L);
};
```

## Registering it

All built-in modules are installed from `varn::lua::NativeModuleRegistry::installAll` in `modules/core/src/lua/NativeModules.cpp`.

1. Write your module class as above.
2. Call `YourModule::install(L)` from `NativeModuleRegistry::installAll`.
3. Mind the order. Anything your module depends on must be installed first. For example, `Promise::installMetatable(L)` runs early so modules like `fs` can return promises even before Lua calls `require("async")`.

## Synchronous functions

Follow the normal `lua_CFunction` rules. Read arguments with `luaL_check*` (or `varn::lua::LuaHelpers::checkString`), push results, and return the result count. Only call Lua from the thread that owns `L`, which is the main runtime thread.

## Async functions

Follow the pattern in `modules/fs/src/FsModule.cpp`:

1. Get the runtime: `varn::lua::LuaHelpers::getRuntime(L)`.
2. Create a `Promise` and post the blocking work to `runtime.taskPool()`.
3. From the worker, call `promise->resolve`, `resolveCustom`, or `reject`. Use `resolveCustom` when the result is a Lua value such as userdata, not a plain string. Its closure runs on the main loop.
4. Push the promise with `Promise::push(L, promise)` and return `1`.

On Emscripten there are no worker threads, so `TaskPool::post` queues the job instead of running it on a background thread. See [async.md](async.md) for the threading rules.

## Examples of existing modules

- `platform`: `PlatformModule::install` registers `require("platform")` with fields like `os`, `arch`, `cpuCount`, and `getLibraryPathByName`. Code is in `modules/platform/src/`. See [lua-api.md](lua-api.md).
- `zip`: `ZipModule::install` registers `require("zip")` when `VARN_ENABLE_ZIP` is on. `zip.extract`, `zip.create`, and `zip.list` use `Promise` and `TaskPool` like `fs`. When zip is off, the functions error with a clear message.
- `http`: `HttpServerModule::install` is always called. It registers `http.createServer` and `http.client.request` on the `http` table. CMake picks the server and client backends.
- `socket`: `SocketModule::install` registers `require("socket")` with `socket.tcp.connect`, `socket.tcp.listen`, and socket/listener methods. On native builds, I/O runs on the task pool. On Emscripten it is a `DUMMY` stub.
- `ffi`: `FfiModule::install` registers `require("ffi")`. With `VARN_FFI_DRIVER=LIBFFI` it links libffi for real calls. With `DUMMY` it exposes the same names but every call errors.

## HTTP handlers run as coroutines

When the full HTTP stack is active, the runtime pushes `req` and `res` and resumes your callback as a coroutine. Use `Promise:await()` for async work inside it. While the coroutine is yielded, the request stays open until it finishes and the response is sent.

## Dummy drivers

Every module that has a CMake driver also ships a no-vendor `DUMMY` backend under `src/drivers/dummy/`, so the link always resolves exactly one implementation. The dummy backends compile and load, but their operations error when used. This keeps the build working on platforms where a real backend is not available.

## Adding a new CMake driver

See [build.md](build.md#adding-a-driver).

## Headers and layout

- Lua registration and `lua_CFunction` code live under `modules/<module>/src/`.
- Public headers live under `modules/<module>/include/varn/<module>/`, listed by `modules/<module>/<module>.cmake`.
