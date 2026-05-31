<p align="center">
    <a href="https://github.com/paulocoutinhox/varn" target="_blank" rel="noopener noreferrer">
        <img width="250" src="extras/images/logo.png" alt="Varn Logo">
    </a>
</p>

# Varn

[![Linux](https://github.com/paulocoutinhox/varn/actions/workflows/build-linux.yml/badge.svg)](https://github.com/paulocoutinhox/varn/actions/workflows/build-linux.yml)
[![macOS](https://github.com/paulocoutinhox/varn/actions/workflows/build-macos.yml/badge.svg)](https://github.com/paulocoutinhox/varn/actions/workflows/build-macos.yml)
[![Windows](https://github.com/paulocoutinhox/varn/actions/workflows/build-windows.yml/badge.svg)](https://github.com/paulocoutinhox/varn/actions/workflows/build-windows.yml)
[![Apple](https://github.com/paulocoutinhox/varn/actions/workflows/build-apple.yml/badge.svg)](https://github.com/paulocoutinhox/varn/actions/workflows/build-apple.yml)
[![Android](https://github.com/paulocoutinhox/varn/actions/workflows/build-android.yml/badge.svg)](https://github.com/paulocoutinhox/varn/actions/workflows/build-android.yml)
[![WebAssembly](https://github.com/paulocoutinhox/varn/actions/workflows/build-wasm.yml/badge.svg)](https://github.com/paulocoutinhox/varn/actions/workflows/build-wasm.yml)

Varn is a small C++20 runtime that embeds Lua and gives your scripts a Node-like set of
built-in modules: HTTP server and client, TCP sockets, async tasks, files, crypto, zip,
JSON/XML, and FFI. Lua runs on one thread; blocking work goes to a worker pool and comes
back through promises, so request handlers never block.

The same core ships two ways: the `varn` command-line host, and an embeddable library
(`Varn.xcframework` on Apple, an `.aar` on Android, a static library elsewhere).

## Quickstart

```bash
python3 varn.py build
./build/bin/varn modules/http/lua/examples/server_demo.lua
```

Then open <http://localhost:3000>.

```lua
local http = require("http")

http.createServer(function(req, res)
    res:json({ hello = req.query.name or "world" })
end):listen(3000)
```

Each module keeps runnable examples under `modules/<module>/lua/examples/`.

## Modules

| `require(...)` | what it gives you |
|----------------|-------------------|
| `http` | HTTP/HTTPS server and client, static files, JSON/XML responses |
| `socket` | TCP client and server |
| `async` | `sleep`, `spawn`, and promises you `:await()` |
| `fs` | async read/write, sync `exists` |
| `crypto` | digests, HMAC, random bytes |
| `zip` | create, extract, and list archives |
| `ffi` | call functions from C libraries |
| `platform` | OS, architecture, cpu count, library paths |
| `log` | `debug` / `info` / `warn` / `error` |

Full reference: [docs/lua-api.md](docs/lua-api.md).

## Building

`varn.py` builds every target. Run `python3 varn.py <task> --help` for options.

| command | output |
|---------|--------|
| `python3 varn.py build` | the `varn` executable for your desktop OS |
| `python3 varn.py apple` | `Varn.xcframework` (iOS, tvOS, watchOS, visionOS, macOS) |
| `python3 varn.py android` | an Android `.aar` (one `libvarn.so` per ABI) |
| `python3 varn.py wasm` | the `varn_wasm` WebAssembly build |
| `python3 varn.py serve` | build wasm and serve the browser demo |

You can also use CMake directly:

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Options and platform notes: [docs/build.md](docs/build.md).

## Requirements

- A C++20 compiler (Clang, GCC, or MSVC)
- CMake 3.26+
- Python 3 (to run `varn.py`)

Lua and the other libraries are downloaded and built for you. Supported native platforms:
Linux, macOS, Windows, iOS, and Android.

## Embedding in your app

Link the library and drive it through the C API in
[modules/api/include/varn/varn.h](modules/api/include/varn/varn.h):

```c
varn_runtime* rt = varn_runtime_new();
varn_runtime_run_file(rt, "main.lua");
varn_runtime_free(rt);
```

## Documentation

| Topic | File |
|-------|------|
| Lua API reference | [docs/lua-api.md](docs/lua-api.md) |
| Building and options | [docs/build.md](docs/build.md) |
| Adding a C++ module | [docs/native-modules.md](docs/native-modules.md) |
| How it works | [docs/architecture.md](docs/architecture.md) |
| Async and promises | [docs/async.md](docs/async.md) |
| Bundled libraries | [docs/official-libraries.md](docs/official-libraries.md) |

## Support

If this project saved you time, consider supporting it:
[GitHub Sponsors](https://github.com/sponsors/paulocoutinhox) · [Ko-fi](https://ko-fi.com/paulocoutinho).

Made with care by [Paulo Coutinho](https://github.com/paulocoutinhox). Licensed under [MIT](LICENSE.md).
