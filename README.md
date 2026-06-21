<p align="center">
    <a href="https://github.com/paulocoutinhox/varn" target="_blank" rel="noopener noreferrer">
        <img width="200" src="extras/images/logo.png" alt="Varn Logo">
    </a>
</p>

# Varn

<p align="center">
    <a href="https://github.com/paulocoutinhox/varn/actions/workflows/build-linux.yml"><img src="https://github.com/paulocoutinhox/varn/actions/workflows/build-linux.yml/badge.svg" alt="Linux"></a>
    <a href="https://github.com/paulocoutinhox/varn/actions/workflows/build-macos.yml"><img src="https://github.com/paulocoutinhox/varn/actions/workflows/build-macos.yml/badge.svg" alt="macOS"></a>
    <a href="https://github.com/paulocoutinhox/varn/actions/workflows/build-windows.yml"><img src="https://github.com/paulocoutinhox/varn/actions/workflows/build-windows.yml/badge.svg" alt="Windows"></a>
    <br>
    <a href="https://github.com/paulocoutinhox/varn/actions/workflows/build-apple.yml"><img src="https://github.com/paulocoutinhox/varn/actions/workflows/build-apple.yml/badge.svg" alt="Apple"></a>
    <a href="https://github.com/paulocoutinhox/varn/actions/workflows/build-android.yml"><img src="https://github.com/paulocoutinhox/varn/actions/workflows/build-android.yml/badge.svg" alt="Android"></a>
    <a href="https://github.com/paulocoutinhox/varn/actions/workflows/build-wasm.yml"><img src="https://github.com/paulocoutinhox/varn/actions/workflows/build-wasm.yml/badge.svg" alt="WebAssembly"></a>
</p>

Varn is an all-in-one platform for building applications with Lua scripts, powered by a fast C++ core. The idea is to give a project everything it needs from the start — a web layer, networking, background work, storage, data protection, and packaging — already built in, consistent, and the same on every device.

Instead of assembling and wiring together separate pieces, you get one complete and coherent foundation that behaves the same whether it runs on a computer, a phone, or in the browser, so the focus stays on the product and not on the plumbing.

## 🚀 Quickstart

```bash
python3 varn.py build
./build/bin/varn modules/http/lua/examples/server_demo.lua
```

Then open <http://localhost:3000>.

A working web server in a few lines:

```lua
local http = require("http")

local server = http.createServer(function(req, res)
    res:json({ hello = req.query.name or "world" })
end)

server:listen(3000)
```

Every feature ships with runnable examples under `modules/<module>/lua/examples/`.

## ✨ Features

Every module is independent and used through `require`, so you pull in only what a script needs.

| Module | What you get |
|----------------|--------------|
| 🌐 `http` | Web server and client, routing, middleware, WebSockets, SSE, and static files |
| 🔌 `socket` | TCP, TLS, UDP, and unix-domain connections |
| ⏳ `async` | Background tasks, timers, promises, and combinators |
| 📁 `fs` | Read, write, stream, stat, and list files |
| 🔐 `crypto` | Hashing, HMAC, password hashing, encryption, encoding, UUIDs, and secure random |
| 🧾 `json` | Encode and decode JSON with a full Lua mapping |
| 🧬 `xml` | Encode and decode XML with a full Lua mapping |
| 🗜️ `zip` | Create, extract, and list archives |
| 🧩 `ffi` | Call functions from native libraries |
| 🖥️ `platform` | System, architecture, processor, and path information |
| ⚙️ `process` | Run commands and read the environment, working directory, and arguments |
| 🕒 `datetime` | Parse, format, and do calendar arithmetic on instants, with ISO-8601 and fixed offsets |
| 📝 `log` | Leveled, structured logging to the console or a file |

Full feature reference: [docs/lua-api.md](docs/lua-api.md).

## 🧱 Components

Pure-Lua components built on top of the modules. Drop one in and `require` it.

| Component | What you get |
|----------------|--------------|
| 🗄️ `vdo` | PDO-style database access with drivers for SQLite, MySQL, and PostgreSQL |
| 🔴 `redis` | Redis client with full command coverage and pipelining |
| 🔧 `env` | Load `.env` files and read typed environment variables |
| ✅ `validate` | Validate tables against a schema with clear error messages |
| 🧪 `test` | A tiny test runner with `describe`/`it` and expectations |
| 🔁 `retry` | Retry with backoff and simple interval scheduling |

Every component ships with runnable examples under `components/<component>/examples/`. Full component reference: [docs/components.md](docs/components.md).

## 🌍 Runs everywhere

The same scripts run on Linux, macOS, and Windows, on iPhone and Android, and in the browser. You can run them as a standalone app you launch, or embed them inside an app you already have.

## ⚡ Performance

The same `/plaintext` and `/json` routes with no framework on any side, driven by `wrk` on Linux — Varn against raw Node `http` and a raw ASGI app on uvicorn (`uvloop`+`httptools`):

| Scenario | Varn | Node | Python |
|----------|-----:|-----:|-------:|
| 1 core, plaintext | **147k req/s** | 55k | 54k |
| 1 core, json | **120k req/s** | 53k | 48k |
| 4 workers, plaintext | **452k req/s** | 199k | 201k |
| 4 workers, json | **410k req/s** | 184k | 186k |

About **2.5× the throughput of Node and Python per core**, scaling near-linearly with `VARN_WORKERS`.

It holds up on real work too. A second benchmark adds `/db` (a MySQL `SELECT`) and `/cache` (a Redis `INCR`), each over a pooled, non-blocking connection — Varn's MySQL client speaks the wire protocol natively over its async socket:

| Scenario | Varn | Node | Python |
|----------|-----:|-----:|-------:|
| db (MySQL `SELECT`) | **13.5k req/s** | 10.6k | 10.7k |
| cache (Redis `INCR`) | **44.4k req/s** | 35.0k | 15.7k |

Varn **leads every route**, the database and cache included. Its MySQL client speaks the wire protocol natively over the async socket, and its redis client auto-pipelines concurrent commands onto one multiplexed connection — so it edges out `ioredis` on `/cache`. Tail latency is in another class: `/db` p99 of 26 ms against Node's 421 ms and `/cache` p99 of 8 ms against Node's 72 ms, because no GC pauses the event loop. Reproduce it with `python3 varn.py bench`. Method and caveats: [docs/stress-test.md](docs/stress-test.md).

## 📚 Documentation

| Topic | File |
|-------|------|
| 📖 Feature reference | [docs/lua-api.md](docs/lua-api.md) |
| 🧱 Components | [docs/components.md](docs/components.md) |
| 🛠️ Building and running | [docs/build.md](docs/build.md) |
| ⏳ Async and promises | [docs/async.md](docs/async.md) |
| 🔒 Safety notes | [docs/security.md](docs/security.md) |
| 🔥 Stress testing | [docs/stress-test.md](docs/stress-test.md) |
| 🐳 Docker | [docs/docker.md](docs/docker.md) |
| 🧭 Platform availability | [docs/platform-availability.md](docs/platform-availability.md) |
| 🧭 Roadmap | [docs/roadmap.md](docs/roadmap.md) |

## 💜 Support

If this project saved you time, consider supporting it:
[GitHub Sponsors](https://github.com/sponsors/paulocoutinhox) · [Ko-fi](https://ko-fi.com/paulocoutinho).

Made with care by [Paulo Coutinho](https://github.com/paulocoutinhox).

Licensed under [MIT](LICENSE.md).
