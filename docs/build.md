# Building Varn

## Requirements

- A C++20 compiler (Clang, GCC, or MSVC)
- CMake 3.26+
- Python 3 (to run `varn.py`)

Everything else — Lua, Poco, OpenSSL, libffi, and so on — is downloaded and built on the
first configure. Nothing else needs to be installed by hand.

## Build a target

`varn.py` is the entry point. Run `python3 varn.py <task> --help` for a task's options.

| command | output |
|---------|--------|
| `python3 varn.py build` | the `varn` executable for your desktop OS |
| `python3 varn.py apple` | `Varn.xcframework` for all Apple platforms |
| `python3 varn.py android` | an Android `.aar` (one `libvarn.so` per ABI) |
| `python3 varn.py wasm` | the `varn_wasm` WebAssembly build |
| `python3 varn.py serve` | build wasm and serve the browser demo |
| `python3 varn.py format` / `clean` / `zip` | project chores |

Or call CMake directly:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The per-task code is in [tools/core](../tools/core); per-platform settings (Apple slices,
Android ABIs) are in [tools/core/targets](../tools/core/targets).

## Choosing backends

Each module picks one backend ("driver"). The defaults work out of the box; override them
with `-D` on `cmake`, or `--define` on `python3 varn.py build`:

```bash
python3 varn.py build --define VARN_LOG_DRIVER=STDOUT --define VARN_FS_DRIVER=DUMMY
```

| variable | options (default first) | selects |
|----------|-------------------------|---------|
| `VARN_HTTP_SERVER_DRIVER` | `POCO`, `DUMMY` | HTTP server |
| `VARN_HTTP_CLIENT_DRIVER` | `POCO`, `DUMMY` | HTTP client |
| `VARN_SOCKET_DRIVER` | `POCO`, `DUMMY` | TCP sockets |
| `VARN_JSON_DRIVER` | `NLOHMANN`, `DUMMY` | `res:json()` |
| `VARN_XML_DRIVER` | `PUGIXML`, `DUMMY` | `res:xml()` |
| `VARN_CRYPTO_DRIVER` | `OPENSSL`, `DUMMY` | `crypto` module |
| `VARN_LOG_DRIVER` | `SPDLOG`, `STDOUT`, `DUMMY` | logging |
| `VARN_FS_DRIVER` | `STD`, `DUMMY` | `fs` module |
| `VARN_FFI_DRIVER` | `LIBFFI`, `DUMMY` | `ffi` module |

There are also two on/off options: `VARN_ENABLE_TLS` (HTTPS, default on — it pulls in
OpenSSL) and `VARN_ENABLE_ZIP` (the `zip` module, default on). A `DUMMY` driver still loads
the module, but its functions return an error when called.

## Platforms

Native hosts: Linux, macOS, Windows, iOS, and Android. `python3 varn.py apple` and `android`
cross-build the embeddable library and bundle it as an xcframework or `.aar`.

Some platforms cannot run everything. tvOS, watchOS, and visionOS block the process and
dynamic-loading APIs that Poco and libffi rely on, so on those slices `http`, `socket`, and
`ffi` fall back to their `DUMMY` drivers. WebAssembly is similar: no in-page server or raw
TCP, and the HTTP client uses the browser's `fetch()`; TLS is off there. These per-platform
choices live in [tools/core/targets/apple.py](../tools/core/targets/apple.py) and
`CMakeLists.txt`.

### WebAssembly

```bash
python3 varn.py wasm        # build varn_wasm
python3 varn.py serve       # build it and open the browser demo
```

The browser shell (Vite) is in `apps/wasm`. See [architecture.md](architecture.md) for how
the runtime maps onto the browser.

## Project layout

Each module is self-contained under `modules/<module>/`:

- `<module>.cmake` — its sources, its driver, and which dependencies it needs.
- `include/` and `src/` — public headers and implementation (one `src/drivers/<id>/` per backend).
- `lua/examples/` — runnable example scripts.

`modules/core/` is the dependency-free nucleus (runtime, event loop, Lua engine, logging).
`cmake/dependencies.cmake` downloads a library only when a chosen driver asks for it through a
`VARN_NEEDS_<dep>` flag.

## Adding a driver

1. Add `modules/<module>/src/drivers/<id>/` implementing the module's contract.
2. In `modules/<module>/<module>.cmake`, add a branch that lists the sources and, if it needs
   a new library, sets a `VARN_NEEDS_<dep>` flag.
3. Fetch that library in `cmake/dependencies.cmake`, keyed on the flag.

Only one driver is linked per module.
