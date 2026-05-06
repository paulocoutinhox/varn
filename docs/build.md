# Build and CMake drivers

Vendor packages and version pins are **not** listed here. For the mapping from **driver ids** to the libraries Varn vendors today, read **[official-libraries.md](official-libraries.md)** once and link to it from release notes when you bump pins.

## Requirements

- **CMake 3.22+**
- **C++20** compiler (Clang, GCC, or MSVC). **`ffi`** defaults to **`LIBFFI`** on all native hosts except **Emscripten**; use **`VARN_FFI_DRIVER=DUMMY`** to disable. CMake **`cmake/varn-libffi.cmake`** uses a system/SDK **libffi** when found, otherwise downloads the **official release tarball from [libffi on GitHub](https://github.com/libffi/libffi/releases)** (`VARN_FETCH_LIBFFI`, on by default) and builds a static library. **Windows** needs **Git for Windows** (`sh.exe`) and **MSVC `nmake` in PATH** for that vendored build. See **Supported platforms** below.
- **Emscripten** (only for the `varn_wasm` target)

## Supported platforms

These **native** OSes are **first‑class targets** for the Varn host (Lua, `http` / `socket` / `fs` / … depending on drivers):

| OS | Host build | `ffi` (`VARN_FFI_DRIVER`) |
|----|----------------|-----------------------------|
| **Linux** | Native GCC/Clang | Default **`LIBFFI`**: pkg-config / `find_library`, or **vendored build** from the official tarball if none is found (`VARN_FETCH_LIBFFI=ON`). |
| **macOS** | Native Clang | Same as Linux. |
| **Windows** | Native MSVC | Default **`LIBFFI`**: CMake builds **libffi** from the **GitHub release tarball** using upstream **`msvcc.sh`**, **Git `sh.exe`**, and **`nmake`** (CI uses **`ilammy/msvc-dev-cmd`** so `cl`/`nmake` are on `PATH`). **`ffi.load`** uses **`LoadLibrary`** (`src/varn/ffi/platform/varn_ffi_dl.c`). |
| **Android** | NDK cross‑compile | Default **`LIBFFI`**: same as Linux when no preinstalled libffi is visible; CMake may **build the tarball** for the NDK ABI (`--host=…`). |
| **iOS** | Xcode cross‑compile | Default **`LIBFFI`**: prefers **SDK `libffi`** when headers/libs are present; otherwise tarball build with iOS **CFLAGS** (slower; may need a clean cache if configure fails). |

**WebAssembly** (`varn_wasm`) is a **separate** target: no desktop **`varn`** binary (no POCO/OpenSSL, no in-page HTTP server or raw TCP). The browser build still wires **`log`**, **`fs`**, **`http.client`**, JSON/XML serializers, and **`async`** with Emscripten-appropriate backends (see [Emscripten](#emscripten-wasm) below); **`ffi`** stays **`DUMMY`**.

**`VARN_FFI_DRIVER=LIBFFI`** uses [**libffi**](https://github.com/libffi/libffi) for calling conventions (same line as [Sourceware](https://sourceware.org/libffi/)); Varn does not fork it. **CPU / ABI** coverage follows the libffi you link (distro, Apple SDK, or CMake-built tarball from **GitHub releases**). **`ffi.load` / `ffi.C`** use **`dlopen`** on Unix and **`LoadLibrary` / `GetProcAddress`** on Windows (`src/varn/ffi/platform/varn_ffi_dl.c`).

First configure downloads embedded **Lua** and any drivers you enable via **CPM** (see `cmake/cpm/`). The Lua language pin lives in **`varn-dependencies.cmake`** and is summarized once under **Lua runtime** in [official-libraries.md](official-libraries.md#lua-runtime).

## Default desktop build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Example with TLS enabled (driver rules still apply; see table below):

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DVARN_ENABLE_TLS=ON
cmake --build build -j
```

## Driver cache variables

Each **module concern** has exactly one active **driver id** (uppercase string). CMake validates combinations. Unknown ids fail at **configure** time.

| Variable | Allowed ids | What it selects |
|----------|-------------|------------------|
| `VARN_HTTP_SERVER_DRIVER` | `POCO`, `DUMMY` | **HTTP server** (`http.createServer`, `listen`, static files). `DUMMY` installs a stub `http` table (no listen). |
| `VARN_HTTP_CLIENT_DRIVER` | `POCO`, `DUMMY`, `EMSCRIPTEN_FETCH` | **HTTP client** (`http.client.request`). `DUMMY` throws when a request is performed. **`EMSCRIPTEN_FETCH`** is **WebAssembly only** (CMake rejects it on native hosts). |
| `VARN_SOCKET_DRIVER` | `POCO`, `DUMMY` | **TCP sockets** (`socket.tcp.*`). `DUMMY` throws on connect/listen. |
| `VARN_JSON_DRIVER` | `DUMMY`, `NLOHMANN` | **JSON** for `res:json()` when **`VARN_HTTP_SERVER_DRIVER=POCO`**. **`NLOHMANN`** uses [nlohmann/json](https://github.com/nlohmann/json). **Poco is not used as a JSON driver** (HTTP/socket only). |
| `VARN_XML_DRIVER` | `DUMMY`, `PUGIXML` | **XML** for `res:xml()` when the HTTP server stack is active. **`PUGIXML`** uses [pugixml](https://github.com/zeux/pugixml). If **`NATIVE`** is still present in the CMake cache, it is mapped to **`PUGIXML`** with a warning. |
| `VARN_CRYPTO_DRIVER` | `OPENSSL`, `DUMMY` | **Crypto** primitives used by the `crypto` module (and TLS prerequisites). |
| `VARN_LOG_DRIVER` | `STDOUT`, `DUMMY`, `SPDLOG` | **Lua `log` + C++ host logging**. Default **`SPDLOG`**; **`STDOUT`** uses **stdout**; **`DUMMY`** discards output. |
| `VARN_FS_DRIVER` | `STD`, `DUMMY` | **`fs` module** storage: normal host filesystem, or disabled (operations fail / `exists` is false). |
| `VARN_FFI_DRIVER` | `LIBFFI`, `DUMMY` | **`ffi` module** (vendored lua-ffi parser + **libffi**): default **`LIBFFI`** on native **Linux, macOS, Windows, iOS, Android**; **`DUMMY`** disables FFI. **Emscripten** forces **`DUMMY`**. Resolution order is in **`cmake/varn-libffi.cmake`** (system / SDK / GitHub release tarball). |

### `VARN_ENABLE_ZIP` (option, not a driver id)

| Setting | Effect |
|---------|--------|
| **`ON`** (default) | Builds the Lua **`zip`** module backed by **libzip** + vendored **zlib** from **CPM** (`zip.create` / `zip.extract` on the task pool). Applies to **native** and **Emscripten** configures. |
| **`OFF`** | No **libzip** fetch; **`require("zip")`** still loads a table whose methods **error** with a clear message. |

Rules enforced today:

- `VARN_ENABLE_TLS=ON` ⇒ `VARN_CRYPTO_DRIVER=OPENSSL`.
- When **`VARN_HTTP_SERVER_DRIVER=POCO`**, CMake requires **`VARN_JSON_DRIVER=NLOHMANN`** or **`DUMMY`** for `res:json()` (use **`NLOHMANN`** for a working **`res:json()`**; see `CMakeLists.txt`).
- **`EMSCRIPTEN_FETCH`** is only valid for **Emscripten** builds; native hosts must use **`POCO`** or **`DUMMY`** for the HTTP client.

`VARN_CRYPTO_DRIVER` defaults follow `VARN_ENABLE_TLS` on first configure unless you override the cache.

## Layout in the tree

Driver sources live under:

- `src/varn/http/drivers/<id>/`
- `src/varn/socket/drivers/<id>/`
- `src/varn/json/drivers/<id>/`
- `src/varn/xml/drivers/<id>/`
- `src/varn/crypto/drivers/<id>/`
- `src/varn/fs/drivers/<id>/`
- `src/varn/log/drivers/<id>/`
- `src/varn/ffi/drivers/<id>/` (dummy stub), `src/varn/ffi/vendor/` (lua-ffi C core when **`VARN_FFI_DRIVER=LIBFFI`**), and `src/varn/ffi/platform/` (dynamic loader glue)

When a driver id selects **no** third-party stack (for example **`DUMMY`** for XML), the tree still links a **`drivers/dummy/`** implementation so every module has exactly one object file implementing its C++ contract. **spdlog**, **nlohmann/json**, and **pugixml** are always fetched via **CPM** when configuring the repo; **`varn`** links them only when **`SPDLOG`**, **`NLOHMANN`**, or **`PUGIXML`** is active, respectively. The legacy id **`NONE`** is accepted once at configure time and mapped to **`DUMMY`** with a warning. The legacy id **`STDERR`** for **`VARN_LOG_DRIVER`** is mapped to **`STDOUT`** with a warning.

Shared C++ declarations stay under `include/varn/`.

## Adding a new driver

1. Pick the **module** (`http`, `socket`, `ffi`, `json`, `xml`, `crypto`, `fs`, `log`).
2. Add `src/varn/<module>/drivers/<id>/` implementing the module’s C++ contract.
3. Extend `CMakeLists.txt` (sources + `VARN_<MODULE>_DRIVER_<ID>=1` defines) and `cmake/cpm/varn-dependencies.cmake` if a new package is needed.
4. Extend the cache `STRINGS`, document the new id **here** (behavior + CMake rules), and add a row to **[official-libraries.md](official-libraries.md)** when the driver pins a **vendor** library (in-tree or dummy drivers may have no third-party row).

Only one driver object may be linked per module.

## CI (iOS / Android / Windows / Linux)

**Linux** and **macOS** jobs use **`LIBFFI`** (system libffi when visible, otherwise CMake builds the **GitHub release tarball**). **Windows** uses **`LIBFFI`** with **Git Bash + MSVC** (`ilammy/msvc-dev-cmd` + vendored libffi build). **iOS** and **Android** use **`LIBFFI`** as resolved by **`cmake/varn-libffi.cmake`**. All mobile/desktop jobs pass **`VARN_ENABLE_TLS=OFF`** so **`VARN_CRYPTO_DRIVER`** is **`DUMMY`** unless you change it. Fresh **desktop** configures use **`POCO`** HTTP server/client/socket, **`NLOHMANN`** JSON, **`PUGIXML`** XML, **`SPDLOG`** log, and **`STD`** `fs` unless the cache overrides them.

## Emscripten (WASM)

```bash
emcmake cmake -B build/wasm -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm -j
```

On **Emscripten**, **`CMakeLists.txt` forces** **`VARN_ENABLE_TLS=OFF`**, **`VARN_HTTP_SERVER_DRIVER=DUMMY`** (no in-page listen), **`VARN_SOCKET_DRIVER=DUMMY`**, **`VARN_CRYPTO_DRIVER=DUMMY`**, and **`VARN_FFI_DRIVER=DUMMY`**. It sets **`VARN_HTTP_CLIENT_DRIVER=EMSCRIPTEN_FETCH`**, **`VARN_JSON_DRIVER=NLOHMANN`**, **`VARN_XML_DRIVER=PUGIXML`**, **`VARN_LOG_DRIVER=SPDLOG`**, and **`VARN_FS_DRIVER=STD`** (MEMFS / normal I/O in the browser)—the same **JSON / XML / log** stack as desktop defaults, with browser **`fetch()`** (**`EM_JS`**) for outbound HTTP. **`varn_wasm`** links **spdlog**, **nlohmann/json**, and **pugixml**; with the default **`VARN_ENABLE_ZIP=ON`**, it also links **libzip** and **zlib** from **CPM** (same as native). Link flags include **`-s ASYNCIFY`**, **`-s FILESYSTEM=1`**, and **`--bind`** (see **`CMakeLists.txt`** for the full list).

**OpenSSL and WASM:** upstream **OpenSSL** can be configured for **Emscripten** / **WebAssembly** (see OpenSSL’s own platform notes and community build recipes). **Varn does not link OpenSSL into `varn_wasm` today**: the Emscripten branch above keeps **`VARN_CRYPTO_DRIVER=DUMMY`** so the default browser binary stays smaller and avoids a second OpenSSL link path. Shipping **`crypto.digest` / `crypto.hmac`** in WASM would mean extending CMake (OpenSSL or another digest/HMAC backend) and accepting the size cost.

**Note on `ffi` and WASM:** upstream **libffi** does ship a **WebAssembly** implementation ([`src/wasm`](https://github.com/libffi/libffi/tree/master/src/wasm) in [libffi/libffi](https://github.com/libffi/libffi)), and the project documents **wasm32 / wasm64** targets. **`varn_wasm` does not integrate that yet**: the Lua **`ffi`** module remains the **`DUMMY`** stub.

**Raw TCP** in the page is not the POCO `socket` module; the WASM host uses **`DUMMY`** for **`socket`**.

The **browser WASM shell** (`apps/wasm`, Vite) is in the root [README](../README.md) (`make wasm` then `make app-wasm`). For the **WASM HTTP client** (current **`fetch()`** bridge and optional follow-ups), see [wasm-http-roadmap.md](wasm-http-roadmap.md).

## Makefile shortcuts

`make configure` passes **`HTTP_SERVER`**, **`HTTP_CLIENT`**, **`SOCKET`**, **`TLS`**, **`LOG`**, **`JSON`**, **`XML`**, **`ZIP`**, **`FS`**, and **`FFI`** to the matching **`VARN_*_DRIVER`** / **`VARN_ENABLE_TLS`** / **`VARN_ENABLE_ZIP`** CMake cache variables. **`make wasm`** builds **`varn_wasm`** under **`build/wasm`**; **`make app-wasm`** depends on that and runs the Vite build for **`apps/wasm`**. Targets that build something under **`apps/<name>/`** use the **`app-<name>`** prefix (for example **`app-wasm`** for **`apps/wasm`**). Other combinations are CMake-only; see the [Makefile](../Makefile).

## Lua examples

Runnable scripts live under **`apps/lua/examples/`** (see **[apps/lua/examples/README.md](../apps/lua/examples/README.md)**). Most assume **`cwd`** is the **repository root** so paths like **`apps/lua/public`** resolve. Run them individually with **`./build/bin/varn apps/lua/examples/...`** (adjust the binary path to your build directory).

Many **`apps/lua/examples/crypto/*.lua`** scripts need **`VARN_CRYPTO_DRIVER=OPENSSL`** (for example default configure with **`VARN_ENABLE_TLS=ON`**, or **`OPENSSL`** set explicitly); with **`DUMMY`** they **error** unless the script documents a **`SKIP`** path.
