# Official third-party libraries by module

This file is the **only** place we list concrete vendor libraries and versions that ship with Varn today. Elsewhere, documentation describes **modules** (`http`, `socket`, `ffi`, `json`, `xml`, `crypto`, `fs`, `log`, …) and **CMake driver ids**; see [build.md](build.md) for how to select drivers.

Dependencies are pulled with **CPM** on first configure unless noted. Versions match the tags in `cmake/cpm/varn-dependencies.cmake` (adjust there if you bump a pin).

The embedded **Lua language** release is documented **only** in the table below; other docs refer to **Lua** without a version number.

## Lua runtime

| Piece | Library | Version / source |
|-------|---------|-------------------|
| Embedded Lua | [lua/lua](https://github.com/lua/lua) | 5.5.0 (`v5.5.0`) |

## FFI (`VARN_FFI_DRIVER`)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `LIBFFI` | [**libffi**](https://github.com/libffi/libffi) (upstream mirrors [Sourceware](https://sourceware.org/libffi/)). CMake **`cmake/varn-libffi.cmake`** prefers **pkg-config / `find_library` / iOS SDK**, then (when **`VARN_FETCH_LIBFFI=ON`**, default) downloads the **GitHub release tarball** and builds **static libffi** via **ExternalProject** (Unix: autotools; Windows: **`msvcc.sh` + Git `sh.exe` + `nmake`**). Lua parser/runtime is adapted from [zhaojh329/lua-ffi](https://github.com/zhaojh329/lua-ffi) (MIT) under `src/varn/ffi/vendor/`; dynamic loading is **`src/varn/ffi/platform/varn_ffi_dl.c`**. | See [build.md — Supported platforms](build.md#supported-platforms). |
| `DUMMY` | — | `require("ffi")` loads a stub table; all operations error with the active driver id |

## HTTP server (`VARN_HTTP_SERVER_DRIVER`)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `POCO` | [pocoproject/poco](https://github.com/pocoproject/poco) `poco-1.15.2-release` | Foundation, Net, Util, optional TLS pieces as configured |
| `DUMMY` | — | Stub `http` module (`createServer` errors); **`http.client`** may still be present and follows the client driver |

## HTTP client (`VARN_HTTP_CLIENT_DRIVER`)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `POCO` | Same Poco Net package as the server when enabled | `http.client.request` (blocking work on the runtime task pool) |
| `DUMMY` | — | `http.client.request` throws at perform time |
| `EMSCRIPTEN_FETCH` | Browser **`fetch()`** from **`EM_JS`** (no **`-s FETCH=1`**) | **WebAssembly only** (CMake rejects on native). Non-blocking `http.client.request` settling the same **`Promise`** / **`VARN/1`** wire as desktop. |

## TCP sockets (`VARN_SOCKET_DRIVER`)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `POCO` | Same Poco Net package when enabled | `socket.tcp.connect` / `listen` / `send` / `receive` (blocking work on the task pool) |
| `DUMMY` | — | `socket.tcp.connect` / `listen` throw |

## JSON for `http` responses (`VARN_JSON_DRIVER`)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `DUMMY` | — | In-tree **dummy** serializer is linked; `res:json` is only registered when the full HTTP stack and **`NLOHMANN`** are enabled (see build rules) |
| `NLOHMANN` | [nlohmann/json](https://github.com/nlohmann/json) **3.11.3** (`v3.11.3`) | **Default** JSON driver on **desktop** and **Emscripten**; **`res:json()`** with **`VARN_HTTP_SERVER_DRIVER=POCO`**. Pulled via **CPM** in **`cmake/cpm/varn-dependencies.cmake`**. **Poco is not a JSON driver** (HTTP/socket only). |

## XML for `http` responses (`VARN_XML_DRIVER`)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `DUMMY` | — | In-tree **dummy** serializer; **`res:xml()`** is not registered (use **`PUGIXML`** for XML responses). |
| `PUGIXML` | [zeux/pugixml](https://github.com/zeux/pugixml) **1.14** (`v1.14`) | **Default** XML driver on **desktop** and **Emscripten**. Pulled via **CPM**. The removed id **`NATIVE`** is remapped to **`PUGIXML`** at configure time (see **`CMakeLists.txt`**). |

## Crypto / TLS material (`VARN_CRYPTO_DRIVER` and TLS option)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `OPENSSL` | OpenSSL via [openssl-cmake](https://github.com/jimmy-park/openssl-cmake) | Used for `crypto.*` and for TLS when enabled |
| `DUMMY` | — | **Dummy** implementation (`drivers/dummy`); primitives throw; TLS off unless you change rules |

When `VARN_ENABLE_TLS=ON`, CMake requires `VARN_CRYPTO_DRIVER=OPENSSL` so the stack can load key material.

## Lua `log` (`VARN_LOG_DRIVER`)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `STDOUT` | — | **`std::cout`** (line per record, flushed); no extra package |
| `DUMMY` | — | No output |
| `SPDLOG` | [gabime/spdlog](https://github.com/gabime/spdlog) **1.17.0** (`v1.17.0`) | **Default** log driver on **desktop** and **Emscripten**; sets the default **spdlog** logger used by **`varn::log::Log`**. Pulled via **CPM**. |

## `fs` storage (`VARN_FS_DRIVER`)

| Driver id | Official backing (today) | Notes |
|-----------|---------------------------|--------|
| `STD` | — | Host `std::filesystem` I/O |
| `DUMMY` | — | Rejects reads/writes; `exists` is always false |

## Lua `zip` (`VARN_ENABLE_ZIP`)

When **`VARN_ENABLE_ZIP=ON`**, **CPM** adds [**madler/zlib**](https://github.com/madler/zlib) **1.3.1** (`v1.3.1`) as a static dependency, then [**nih-at/libzip**](https://github.com/nih-at/libzip) **1.10.1** (`v1.10.1`) with optional backends disabled for predictable static linking; **`cmake/modules/FindZLIB.cmake`** wires **`find_package(ZLIB)`** to that vendored build so **libzip** sees **`ZLIB::ZLIB`**. The same stack links into **`varn_wasm`** when zip is enabled for **Emscripten**. The Lua surface is **`ZipModule`** (`zip.create` / `zip.extract`). There is no separate **`VARN_ZIP_DRIVER`** string today — only this on/off option.

## WebAssembly (`varn_wasm`)

The WASM target links **vendored Lua**, the **browser** entry (`src/varn/wasm/`), and the same **Varn** sources as the desktop host for modules that have Emscripten backends. It does **not** link **POCO**, **OpenSSL**, or **libffi** on the **default** Emscripten configure. With **`VARN_ENABLE_ZIP=ON`** (the default), it also links **libzip** and **zlib** from **CPM**, same as native builds. **OpenSSL can be built for WASM**, but this project **does not** add that dependency to **`varn_wasm`**; **`CMakeLists.txt`** forces **`DUMMY`** for **crypto** (among other stubs). Emscripten **`CMakeLists.txt`** also forces **`DUMMY`** for **HTTP server**, **TCP socket**, and **`ffi`**; **`EMSCRIPTEN_FETCH`** for the HTTP client; and matches desktop defaults for **`log`** / JSON / XML (**`SPDLOG`**, **`NLOHMANN`**, **`PUGIXML`**) and **`fs`** (**`STD`**). **`VARN_ENABLE_TLS=OFF`**. **spdlog**, **nlohmann/json**, and **pugixml** are added with **CPM** for **all** configures (see **`cmake/cpm/varn-dependencies.cmake`**).

Upstream **libffi** includes a **WASM** port ([`src/wasm`](https://github.com/libffi/libffi/tree/master/src/wasm)); **`varn_wasm` does not compile or link it today**, so the Lua **`require("ffi")`** surface remains a stub in the WASM host despite libffi’s own wasm support.
