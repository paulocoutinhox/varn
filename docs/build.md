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
| `python3 varn.py serve` | the browser demo |

## 🌍 Browser demo

```bash
python3 varn.py serve
```

This builds the browser version and opens the demo.

## 🖥️ Platforms

The same project builds for Linux, macOS, Windows, iPhone, Android, and the browser, with the same features wherever they are available.

Some features are not available everywhere: in the browser there is no built-in web server or raw socket, and a few platforms run a reduced set of features. When a feature is not available on a platform, it still loads, and using it returns a clear error.
