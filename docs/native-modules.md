# Native modules (C++)

## Registration

All built-in modules are installed from `varn::lua::NativeModuleRegistry::installAll` in `src/varn/lua/NativeModules.cpp`.

1. Add a **C++ class** for your Lua surface (for example `FsModule`, `CryptoModule`, `LogModule`, `FfiModule`) with `static void install(struct lua_State* L)` and **private** `static int lua*(struct lua_State* L)` callbacks (same shape as `AsyncModule`). Avoid the identifier `register` as a method name (storage-class history in C++). Keep small helpers as **private static** methods on that class (not free functions in the `.cpp`) so the module stays a single cohesive type.
2. Call `YourModule::install(L)` from `NativeModuleRegistry::installAll` in the right order.
3. Ensure any global Lua infrastructure your module depends on is initialized **before** modules that need it. `Promise::installMetatable(L)` runs first so `fs` and others can return promises even if Lua never `require("async")`.

## Synchronous bindings

Use normal `lua_CFunction` rules on **private static** members of your module class: read arguments with `luaL_check*` (or `varn::lua::LuaHelpers::checkString`), push results, return the result count. Only call Lua from the thread that owns `L` (the main runtime thread when invoked from a main-loop job).

Example sketch:

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

## Async C functions

Follow the pattern in `src/varn/fs/FsModule.cpp`:

- Obtain `Runtime&` from the Lua registry (`varn::lua::LuaHelpers::getRuntime(L)`).
- Create a `Promise`, post work to `runtime.taskPool()`, and from the worker call `promise->resolve` / `resolveCustom` / `reject`. Use **`resolveCustom`** only to push values on the **main Lua state** (for example TCP socket userdata); the closure must be safe to run on the main loop thread.
- Push the promise with `Promise::push(L, promise)` and return `1`.

On **Emscripten**, **`TaskPool::post`** runs the job **immediately** on the calling thread (no background workers), and **`Promise`** resumption does not go through **`EventLoop::post`**—still call **`resolve` / `reject`** from outside the Lua stack that invoked the binding if your code would otherwise recurse into Lua.

See [Concurrency and async](async.md) for threading constraints.

## `platform` module

`PlatformModule::install` registers **`require("platform")`** with **`os`**, **`arch`**, **`hostVersion`**, **`libPrefix`**, **`shlibSuffix`**, **`libraryFilename`**, and **`getLibraryPathByName`** (see [lua-api.md](lua-api.md)). Implementation: `src/varn/platform/PlatformInfo.cpp` + `PlatformModule.cpp`; **`hostVersion`** comes from **`cmake/VarnVersion.h.in`** (configured at CMake time from **`project(... VERSION)`**).

## `zip` module

`ZipModule::install` registers **`require("zip")`** when **`VARN_ENABLE_ZIP`** is on and **libzip** is linked (`src/varn/zip/ZipModule.cpp`). **`zip.extract`**, **`zip.create`**, and **`zip.list`** use **`Promise`** + **`TaskPool`** like **`fs`**. When zip is disabled, these functions error with a clear message.

## HTTP module

`HttpServerModule::install` is **always** called from `NativeModuleRegistry::installAll`. It registers **`http.createServer`** (server driver) and **`http.client.request`** (client driver) on the same **`http`** table. CMake chooses the **server** implementation (`HttpServerModule.cpp` vs a stub under `src/varn/http/drivers/dummy/`) and a separate **client** transport (`PocoHttpClient`, `DummyHttpClient`, or **`EmscriptenFetchHttpClient`** under `src/varn/http/drivers/emscripten_fetch/`). Rules and driver ids: [build.md](build.md). Libraries: [official-libraries.md](official-libraries.md).

## Socket module

`SocketModule::install` registers **`require("socket")`** with **`socket.tcp.connect`**, **`socket.tcp.listen`**, and userdata methods **`send`**, **`receive`**, **`close`** (sockets) and **`accept`**, **`close`** (listeners). On **native POCO** builds, blocking I/O runs on the task pool; results surface on the main loop via **`Promise`** (including **`Promise::resolveCustom`** when the resolved value is Lua userdata, not a string). On **Emscripten**, the socket driver is **`DUMMY`** (stubs that error when used).

## FFI module

`FfiModule::install` registers **`require("ffi")`**. With **`VARN_FFI_DRIVER=LIBFFI`**, the host loads the **vendored C** implementation (lexer + parser) and links **[Sourceware libffi](https://sourceware.org/libffi/)** for calls/closures behind **`extern "C" int luaopen_ffi`**, wired from **`FfiModule.cpp`**. With **`DUMMY`**, **`DummyFfiModule.cpp`** provides **`luaopen_ffi_dummy`** with the same Lua surface names but every call errors.

## Dummy and in-tree drivers

Every concern that has a **CMake driver** also has a **no-vendor** implementation under `drivers/dummy/` so the link always resolves exactly one implementation: crypto **`DUMMY`** → `DummyCryptoPrimitives`, JSON **`DUMMY`** → `DummyJsonSerializer`, XML **`DUMMY`** → `DummyXmlSerializer`, HTTP **server `DUMMY`** → `HttpServerModuleStub`, HTTP **client `DUMMY`** → `DummyHttpClient`, **socket `DUMMY`** → `DummySocketModule`, **FFI `DUMMY`** → `DummyFfiModule` (Lua stub; no libffi), `fs` **`DUMMY`** → `DummyFsStorage`, `log` **`DUMMY`** → no-op sink. **Vendor-backed** paths **`NLOHMANN`**, **`PUGIXML`**, and **`SPDLOG`** are used on **native and WASM** defaults; **`EMSCRIPTEN_FETCH`** is **WASM-only** for the HTTP client—see [build.md](build.md#emscripten-wasm). The obsolete id **`NONE`** is mapped to **`DUMMY`** in `CMakeLists.txt` with a deprecation warning.

The **`async`** module has no CMake driver matrix; it is always the same in-tree implementation on top of **`Promise`** and **`TaskPool`** ( **`emscripten_sleep`** inside the posted job on Emscripten for **`async.sleep`**; see [async.md](async.md#emscripten-varn_wasm)).

When the full stack is active, the handler runs as a **coroutine** on the main loop: the runtime pushes `req` and `res`, then `lua_resume`s your callback. Use `Promise:await()` for async work; when the coroutine yields, the HTTP request stays open until the coroutine completes and the response is finished.

## Adding a new CMake driver

See [Build and CMake drivers](build.md#adding-a-new-driver).

## Headers and layout

- Lua-facing registration and `lua_CFunction` implementations usually live under `src/varn/<area>/`.
- Shared declarations used by multiple translation units live under `include/varn/`.
