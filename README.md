# Varn

[![Build](https://github.com/paulocoutinhox/varn/actions/workflows/build-all.yml/badge.svg)](https://github.com/paulocoutinhox/varn/actions/workflows/build-all.yml)

**Varn** is a **C++20** host that embeds **Lua** and targets **HTTP/HTTPS**-style servers with a small **Node-like** surface: native modules, **coroutine**-based handlers, and **async** work without blocking the single Lua owner thread.

## What you get

- **`http`** — create a server, handle requests, build responses (JSON/XML/static files depending on build).
- **`async`** — sleep and spawn helpers built on the same **promise** model as `fs`.
- **`fs`** — async read/write and sync `exists`.
- **`crypto`** — hashing and random bytes when enabled for your build.
- **`log`** — `debug` / `info` / `warn` / `error` with multiple arguments (like `print`); same pipeline as host C++ (**`varn::log::Log`**). Driver **`VARN_LOG_DRIVER`** (default **`SPDLOG`**, optional **`STDOUT`**) in [docs/build.md](docs/build.md).
- **TLS** — optional HTTPS using cert paths from Lua or environment variables.
- **WebAssembly shell** — `varn_wasm` plus `apps/wasm` (Vite UI, worker-hosted Lua, cooperative stop). The WASM host uses browser **`fetch()`** (**`EM_JS`**) for **`http.client`**, **spdlog** for **`log`**, **MEMFS-capable `fs`**, and the same **`async` / `Promise`** / **`EventLoop`** model with a post-chunk drain for queued work (no POCO HTTP server, no raw TCP, **`crypto`/`ffi`** stubs); see [docs/build.md](docs/build.md#emscripten-wasm), [docs/async.md](docs/async.md#emscripten-varn_wasm), and [docs/lua-api.md](docs/lua-api.md).

Implementation details (which vendored libraries attach to which **driver** ids) are **not** repeated here; see **[docs/official-libraries.md](docs/official-libraries.md)**.

## Documentation

| Topic | Where |
|--------|--------|
| Design overview & doc index | [docs/design.md](docs/design.md) |
| Build, CMake drivers, WASM | [docs/build.md](docs/build.md) |
| Architecture | [docs/architecture.md](docs/architecture.md) |
| Async / `Promise` | [docs/async.md](docs/async.md) |
| Extending in C++ | [docs/native-modules.md](docs/native-modules.md) |
| Lua API reference | [docs/lua-api.md](docs/lua-api.md) |
| Vendor list by module | [docs/official-libraries.md](docs/official-libraries.md) |
| Security notes (trust boundaries) | [docs/security.md](docs/security.md) |
| WASM HTTP client (current + roadmap) | [docs/wasm-http-roadmap.md](docs/wasm-http-roadmap.md) |

## Requirements

- **C++20** (Clang, GCC, or MSVC)
- **CMake 3.22+**
- **Lua** bundled with the project (no system Lua required); language release is pinned in **`cmake/cpm/varn-dependencies.cmake`** and listed once in **[docs/official-libraries.md](docs/official-libraries.md#lua-runtime)**.

Anything beyond that depends on which **drivers** you enable; see [docs/build.md](docs/build.md) and [docs/official-libraries.md](docs/official-libraries.md).

**Supported platforms** for the native host: **Linux**, **macOS**, **Windows**, **iOS**, and **Android** (NDK). **`ffi`** defaults to **`LIBFFI`** (CMake downloads the official tarball from **https://github.com/libffi/libffi/releases** when needed); see [docs/build.md — Supported platforms](docs/build.md#supported-platforms).

## Quickstart

```bash
make build
./build/bin/varn apps/lua/examples/server.lua
```

`apps/lua/examples/server.lua` forwards to **`apps/lua/examples/http/server_demo.lua`**. A full index of runnable scripts by module is in **[apps/lua/examples/README.md](apps/lua/examples/README.md)**.

Or with CMake only (defaults are documented in [docs/build.md](docs/build.md)):

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/varn apps/lua/examples/server.lua
```

`make` variables: **`HTTP_SERVER`**, **`HTTP_CLIENT`**, **`SOCKET`**, **`FFI`**, **`JSON`**, **`XML`** (CMake `VARN_*_DRIVER`), **`TLS`** → `VARN_ENABLE_TLS`, **`LOG`**, **`FS`**, **`SCRIPT`**, **`PORT`** — see [Makefile](Makefile) and [docs/build.md](docs/build.md).

## HTTPS

```bash
VARN_TLS_CERT=cert.pem VARN_TLS_KEY=key.pem VARN_PORT=3443 ./build/bin/varn apps/lua/examples/https.lua
```

## WebAssembly (browser)

```bash
emcmake cmake -B build/wasm -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm -j
cd apps/wasm
VARN_WASM_DIR="../../build/wasm/bin" npm install
VARN_WASM_DIR="../../build/wasm/bin" npm run build
```

Or: `make wasm` then `make app-wasm`. Details: [docs/build.md](docs/build.md#emscripten-wasm), [docs/architecture.md](docs/architecture.md), and [docs/lua-api.md](docs/lua-api.md) (WebAssembly subsection).

## Lua sketch

```lua
local http = require("http")
local fs = require("fs")
local crypto = require("crypto")

http.createServer(function(req, res)
    local text = fs.readFile("apps/lua/public/index.html"):await()
    local hash = crypto.digest("SHA256", text, "hex")
    res:json({ hash = hash, size = #text })
end):listen(3000)
```

Full API: [docs/lua-api.md](docs/lua-api.md).

## ☕ Buy me a coffee

If this project saved you time, consider supporting its development:

- [GitHub Sponsors](https://github.com/sponsors/paulocoutinhox)
- [Ko-fi](https://ko-fi.com/paulocoutinho)

*Made with ❤️ by [Paulo Coutinho](https://github.com/paulocoutinhox).*

## 📜 License

[MIT](LICENSE.md)
