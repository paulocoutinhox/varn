# 🛠️ Building and running

`varn.py` is the entry point. Run `python3 varn.py <task> --help` for a task's options.

## 🚀 Build and run

```bash
python3 varn.py build
./build/bin/varn modules/http/lua/examples/server_demo.lua
```

Then open <http://localhost:3000>.

## 📦 Targets

| command | what it builds |
|---------|----------------|
| `python3 varn.py build` | the desktop app |
| `python3 varn.py apple` | the Apple build (iOS, tvOS, watchOS, visionOS, macOS) |
| `python3 varn.py android` | the Android build |
| `python3 varn.py wasm` | the browser build |
| `python3 varn.py app-wasm` | the browser build bundled into `apps/wasm/dist` |
| `python3 varn.py serve` | the browser demo dev server |
| `python3 varn.py format` | run clang-format over the sources |
| `python3 varn.py clean` | remove the build directory |
| `python3 varn.py zip` | create a source archive |

## ⚙️ Backends

Each module picks an implementation at configure time. Pass overrides through `build` with
`-D`, for example `python3 varn.py build -D VARN_LOG_DRIVER=STDOUT -D VARN_ENABLE_TLS=OFF`.
The `DUMMY` backend keeps the module loadable but makes its calls return a clear
"not available" error, which is what the browser and reduced platforms use.

| Option | Values (default first) | Selects |
|--------|------------------------|---------|
| `VARN_HTTP_SERVER_DRIVER` | `POCO`, `DUMMY` | web server transport |
| `VARN_HTTP_CLIENT_DRIVER` | `POCO`, `EMSCRIPTEN_FETCH`, `DUMMY` | http client transport |
| `VARN_SOCKET_DRIVER` | `POCO`, `DUMMY` | tcp and udp sockets |
| `VARN_CRYPTO_DRIVER` | `OPENSSL`, `DUMMY` | crypto primitives |
| `VARN_JSON_DRIVER` | `NLOHMANN`, `DUMMY` | json serializer |
| `VARN_XML_DRIVER` | `PUGIXML`, `DUMMY` | xml serializer |
| `VARN_LOG_DRIVER` | `SPDLOG`, `STDOUT`, `DUMMY` | log backend |
| `VARN_FS_DRIVER` | `STD`, `DUMMY` | filesystem storage |
| `VARN_FFI_DRIVER` | `LIBFFI`, `DUMMY` | native function calls |
| `VARN_ENABLE_TLS` | `ON`, `OFF` | TLS for http and sockets (needs `VARN_CRYPTO_DRIVER=OPENSSL`) |

## 🌍 Browser demo

```bash
python3 varn.py serve
```

This builds the browser version and opens the demo.

## 🖥️ Platforms

The same project builds for Linux, macOS, Windows, iPhone, Android, and the browser, with the same features wherever they are available.

Some features are not available everywhere: in the browser there is no built-in web server or raw socket, and a few platforms run a reduced set of features. When a feature is not available on a platform, it still loads, and using it returns a clear error.
