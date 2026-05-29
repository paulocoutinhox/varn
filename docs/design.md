# Varn design

A short overview of how the pieces fit together. For details, follow the links below.

## How it fits together

Varn runs your Lua code on a single thread. That thread owns the one and only `lua_State` and runs the event loop.

Anything that would block (file reads, network calls, CPU-heavy work) is sent to a pool of worker threads. When a worker finishes, it hands the result back through a `Promise`, which resumes your Lua code on the main thread. Workers never call Lua directly.

So the flow is always:

```text
worker thread  →  Promise::resolve  →  post to EventLoop  →  resume Lua on main thread
```

HTTP requests work the same way. The transport may use its own threads to accept and parse requests, then posts each one to the main loop, where your handler runs as a Lua coroutine.

## Modules

The code is organized as self-contained modules under `modules/<module>/`. Each module declares its sources in `<module>.cmake` and keeps headers in `include/`, code in `src/`, and runnable scripts in `lua/examples/`.

`modules/core/` is the dependency-free nucleus: the runtime, event loop, task pool, Lua engine, and logging. Other modules (`http`, `fs`, `crypto`, `ffi`, `zip`, and so on) build on top of it.

Each module can have several backends. A backend lives in `src/drivers/<id>/`, and CMake picks one driver per module. See [build.md](build.md) for how drivers are chosen and [official-libraries.md](official-libraries.md) for which library backs each one.

## Library and executable

The same C++ core powers two things:

- The `varn` command-line executable.
- An embeddable library with a small C API in `modules/api/include/varn/varn.h` (`varn_runtime_new`, `varn_runtime_run_file`, `varn_runtime_run_string`, `varn_runtime_stop`, `varn_runtime_free`, `varn_version`).

## WebAssembly

There is also a browser build (`varn_wasm`) under `apps/wasm`. It uses the same core, but with browser-friendly drivers: outbound HTTP goes through the browser `fetch()` API, TLS is off, and there is no HTTP server or raw socket. See [build.md](build.md#webassembly) and [wasm-http-roadmap.md](wasm-http-roadmap.md).

## Where to read more

| Topic | Document |
|-------|----------|
| Threads and the event loop | [architecture.md](architecture.md) |
| Promises, `:await()`, the task pool | [async.md](async.md) |
| Adding a C++ module | [native-modules.md](native-modules.md) |
| Lua API (`http`, `async`, `fs`, `log`, `crypto`, `platform`, `zip`) | [lua-api.md](lua-api.md) |
| Building and drivers | [build.md](build.md) |
| Which library backs each driver | [official-libraries.md](official-libraries.md) |
| Trust boundaries | [security.md](security.md) |
| Browser HTTP client roadmap | [wasm-http-roadmap.md](wasm-http-roadmap.md) |
