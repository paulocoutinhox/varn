# Third-party libraries by module

This is the one place that lists the concrete vendor libraries and versions Varn ships today. Other docs talk about modules and CMake driver ids without versions. See [build.md](build.md) for how to choose drivers.

Dependencies are pulled with CPM on first configure, unless noted. Versions match the tags in `cmake/dependencies.cmake`. A dependency is only fetched when a chosen driver sets a `VARN_NEEDS_<dep>` flag.

## Lua runtime

| Piece | Library | Version |
|-------|---------|---------|
| Embedded Lua | [lua/lua](https://github.com/lua/lua) | 5.5.0 (`v5.5.0`) |

## FFI (`VARN_FFI_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `LIBFFI` | [libffi](https://github.com/libffi/libffi) | CMake (`modules/ffi/libffi.cmake`) prefers a system libffi, then downloads and builds a static copy when `VARN_FETCH_LIBFFI=ON` (default). The Lua parser is adapted from [zhaojh329/lua-ffi](https://github.com/zhaojh329/lua-ffi) (MIT). See [build.md](build.md#platforms). |
| `DUMMY` | — | `require("ffi")` loads a stub; all calls error. |

## HTTP server (`VARN_HTTP_SERVER_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `POCO` | [pocoproject/poco](https://github.com/pocoproject/poco) `poco-1.15.2-release` | Foundation, Net, Util, and optional TLS pieces. |
| `DUMMY` | — | Stub server. `createServer` errors. The client may still work. |

## HTTP client (`VARN_HTTP_CLIENT_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `POCO` | Poco Net | `http.client.request` runs on the task pool. |
| `DUMMY` | — | `http.client.request` errors. |
| `EMSCRIPTEN_FETCH` | Browser `fetch()` | WebAssembly only. Non-blocking, settling the same `Promise` and `VARN/1` wire as desktop. |

## TCP sockets (`VARN_SOCKET_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `POCO` | Poco Net | `socket.tcp` connect, listen, send, receive. Runs on the task pool. |
| `DUMMY` | — | Connect and listen error. |

## JSON (`VARN_JSON_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `NLOHMANN` | [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 (`v3.11.3`) | Default on desktop and WASM. Backs `res:json()` when the HTTP server is `POCO`. |
| `DUMMY` | — | In-tree stub serializer. `res:json` is only registered with the full HTTP stack and `NLOHMANN`. |

## XML (`VARN_XML_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `PUGIXML` | [zeux/pugixml](https://github.com/zeux/pugixml) 1.14 (`v1.14`) | Default on desktop and WASM. The old id `NATIVE` is remapped to `PUGIXML`. |
| `DUMMY` | — | In-tree stub. `res:xml()` is not registered. |

## Crypto and TLS (`VARN_CRYPTO_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `OPENSSL` | OpenSSL via [openssl-cmake](https://github.com/jimmy-park/openssl-cmake) | Used for `crypto.*` and for TLS. |
| `DUMMY` | — | Primitives error; TLS off. |

TLS is on by default. With `VARN_ENABLE_TLS=ON`, CMake requires `VARN_CRYPTO_DRIVER=OPENSSL`. TLS is forced off only on WebAssembly.

## Logging (`VARN_LOG_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `SPDLOG` | [gabime/spdlog](https://github.com/gabime/spdlog) 1.17.0 (`v1.17.0`) | Default on desktop and WASM. |
| `STDOUT` | — | Writes a line per record to `std::cout`. |
| `DUMMY` | — | No output. |

## Filesystem (`VARN_FS_DRIVER`)

| Driver | Library | Notes |
|--------|---------|-------|
| `STD` | — | Host `std::filesystem` I/O. |
| `DUMMY` | — | Reads and writes are rejected; `exists` is always false. |

## Zip (`VARN_ENABLE_ZIP`)

When `VARN_ENABLE_ZIP=ON` (default), CPM adds [madler/zlib](https://github.com/madler/zlib) 1.3.1 (`v1.3.1`) and [nih-at/libzip](https://github.com/nih-at/libzip) 1.10.1 (`v1.10.1`), built static. `cmake/modules/FindZLIB.cmake` wires the vendored zlib so libzip can find it. The Lua surface is `zip.create` and `zip.extract`. There is no separate driver string, only this on/off option. The same stack links into `varn_wasm` when zip is enabled.

## WebAssembly (`varn_wasm`)

The WASM target links the vendored Lua, the browser entry point, and the same Varn sources as desktop for modules that have an Emscripten backend.

On the default WASM configure, CMake forces:

- `crypto`, HTTP server, TCP socket, and `ffi` to `DUMMY`.
- The HTTP client to `EMSCRIPTEN_FETCH`.
- TLS off.

It keeps the desktop defaults for `log`, JSON, XML (`SPDLOG`, `NLOHMANN`, `PUGIXML`), and `fs` (`STD`). It does not link POCO, OpenSSL, or libffi.

OpenSSL and libffi both have WASM ports, but this project does not build them into `varn_wasm` today, so `crypto` and `require("ffi")` remain stubs in the browser.
