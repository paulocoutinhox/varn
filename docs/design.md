# Varn design

High-level overview and **documentation index**. Deeper guides live in **this directory** (`docs/`), linked below.

## Documentation index

| Subject | Document |
|---------|----------|
| Threads, loop, HTTP dispatch | [architecture.md](architecture.md) |
| CMake, drivers, WASM | [build.md](build.md) |
| `Promise`, `:await()`, task pool | [async.md](async.md) |
| C++ modules and drivers | [native-modules.md](native-modules.md) |
| Lua `http`, `async`, `fs`, `log`, `crypto`, `platform`, `zip` | [lua-api.md](lua-api.md) |
| Vendored libraries ↔ modules | [official-libraries.md](official-libraries.md) |
| Trust boundaries (high level) | [security.md](security.md) |
| WASM HTTP client (current + follow-ups) | [wasm-http-roadmap.md](wasm-http-roadmap.md) |

## Main idea (summary)

Varn keeps **one Lua state** on the **main event-loop thread**. Blocking work runs on a **task pool**; completion is delivered through **`Promise`** objects that resume waiters on the main loop. The **HTTP** module uses a **transport driver** that may run I/O on its own threads; each request handler is scheduled as a **Lua coroutine** on the main loop.

```text
HTTP transport thread(s)  →  post(handler job)  →  EventLoop (main)  →  lua_resume(coroutine)
Task pool workers          →  Promise::resolve   →  post(resume job)  →  EventLoop  →  lua_resume(waiters)
```

## CMake drivers (summary)

Each concern (`http`, `json`, `xml`, `crypto`, `fs`, `log`, …) selects **one** active **driver id** via cache variables. Rules and layout: [build.md](build.md). Which **vendor packages** those ids map to: [official-libraries.md](official-libraries.md).

## WebAssembly

The `varn_wasm` target is a browser Lua host in **`apps/wasm`**: no POCO HTTP server, **no OpenSSL-linked `crypto`** (CMake forces **`DUMMY`**; see [build.md](build.md#emscripten-wasm)), **libzip** + **zlib** when **`VARN_ENABLE_ZIP=ON`** (default), and no raw TCP **`socket`**. Outbound **`http.client.request`** uses the browser **`fetch()`** bridge (**`EM_JS`**, driver id **`EMSCRIPTEN_FETCH`**) and the same **`Promise`** / **`VARN/1`** wire as desktop. **`TaskPool`** work is **queued** and drained after each embedded Lua chunk (with **`EventLoop`** jobs and in-flight **`fetch`**); **`Promise`** always settles via **`EventLoop::post`**, matching the desktop model. **`-s ASYNCIFY`** remains for paths such as **`async.sleep`** and the shell line hook. **`log`**, JSON, and XML follow the same **spdlog / nlohmann / pugixml** defaults as desktop where enabled. Optional HTTP client follow-ups (credentials, streaming, …) are listed in [wasm-http-roadmap.md](wasm-http-roadmap.md). See also [architecture.md](architecture.md) and [async.md](async.md#emscripten-varn_wasm).

## Source layout

- `src/varn/<module>/` — runtime and Lua bindings.
- `src/varn/<module>/drivers/<name>/` — driver implementations for that module.
- `include/varn/` — shared C++ APIs between drivers and modules.
- `apps/lua/` — sample Lua under **`examples/`** (see **`apps/lua/examples/README.md`**), static tree under **`public/`** (paths assume **cwd = repo root**).
- `apps/wasm/` — browser shell (Vite); `npm run build` writes `dist/`, and `scripts/copy-wasm.mjs` stages `varn_wasm.{js,wasm}` into `public/wasm/` before the Vite build.
