# 📖 Lua API reference

Every built-in module is available with `require`. Each module has its own page below, with
its full API and every runnable example. The examples also live next to each module under
`modules/<module>/lua/examples/` and run from the project root.

## Modules

| Module | Description |
|--------|-------------|
| [🌐 http](lua-api/http.md) | HTTP server, app framework, client, and WebSockets |
| [🔌 socket](lua-api/socket.md) | Async TCP and UDP sockets |
| [⏳ async](lua-api/async.md) | Coroutines, promises, and the event loop |
| [📁 fs](lua-api/fs.md) | Filesystem read/write/exists |
| [🔐 crypto](lua-api/crypto.md) | Digests, HMAC, and secure random bytes |
| [🧾 json](lua-api/json.md) | JSON encode/decode with full type conversion |
| [🧬 xml](lua-api/xml.md) | XML encode/decode over a Lua node model |
| [🗜️ zip](lua-api/zip.md) | Create, extract, and list ZIP archives |
| [🧩 ffi](lua-api/ffi.md) | Declare and call C functions |
| [🖥️ platform](lua-api/platform.md) | Host and build information |
| [📝 log](lua-api/log.md) | Leveled logging |

## The `arg` global

The host sets `arg` to the command-line arguments (the script path and its parameters). In
the browser build it is usually empty.
