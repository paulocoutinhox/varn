# Architecture overview

## Goals

- One Lua state, one owning thread. The Lua C API is used only from the runtime's main thread. Code running on other threads must never call Lua directly.
- Non-blocking scripts. Blocking I/O and CPU-heavy work run on a worker pool. Results come back through `Promise` objects that resume on the main loop.
- HTTP isolation. The HTTP transport runs I/O on its own threads, then forwards each request to the main loop so your handler always runs in the Lua-owner thread.

## The main loop

```text
┌─────────────────────┐     post(job)      ┌──────────────────┐
│  HTTP transport     │ ─────────────────► │  EventLoop       │
│  or TaskPool worker │                    │  (main thread)   │
└─────────────────────┘                    │  lua_resume /    │
        │                                  │  lua_yield       │
        │                                  └────────┬─────────┘
        │                                           │
        └───────────────────────────────────────────┘
                          Promise::resolve / reject
```

## Components

- `Runtime` owns the `LuaEngine`, `EventLoop`, `TaskPool`, and any running HTTP servers. On shutdown it stops servers, then workers, then the event loop.
- `EventLoop` runs on the thread that started the script. It runs posted jobs one at a time using a queue protected by a mutex and condition variable.
- `TaskPool` is a fixed pool of worker threads for blocking work (for example `fs.readFile`).
- `Promise` connects workers to Lua. `resolve` or `reject` updates the state, then posts the resumption to the `EventLoop`. Waiting Lua coroutines are resumed only on the main loop.

Which library backs each module depends on the chosen CMake driver. See [build.md](build.md) and [official-libraries.md](official-libraries.md).

## How an HTTP request flows

1. A request arrives. An HTTP transport thread accepts and parses it. No Lua runs yet.
2. The transport posts the request to the `EventLoop` and moves on.
3. The main thread picks it up and runs your handler as a Lua coroutine, passing `req` and `res`.
4. If the handler `:await()`s a promise (say a file read), the coroutine pauses and the main
   thread is free to run other jobs. The blocking read happens on a task pool thread.
5. When the read finishes, that worker posts the result to the `EventLoop`. The main thread
   resumes the coroutine right after the `:await()`.
6. The handler finishes the response, and the transport sends it back to the client.

The handler never blocks a transport thread, and Lua is only ever touched on the main thread.
See [async.md](async.md) for the threading rules in detail.

## Logging

The Lua `log` module and host C++ share the same `varn::log::Log` pipeline. `VARN_LOG_DRIVER` picks the sink: `SPDLOG` (default), `STDOUT`, or `DUMMY`. See [build.md](build.md).

## WebAssembly

The browser build (`varn_wasm`) uses the same core with browser-friendly drivers. It does not link the POCO HTTP server, and TLS is off. The HTTP server and raw socket are `DUMMY` stubs.

Outbound `http.client.request` uses the browser `fetch()` API (driver `EMSCRIPTEN_FETCH`) with the same `VARN/1` wire format and the same `Promise` completion path as desktop.

There are no worker threads in the browser. Instead, `TaskPool` jobs are queued and drained by the WASM host after your Lua chunk returns, together with the `EventLoop` jobs. See [async.md](async.md) and [build.md](build.md#webassembly).

## Further reading

- [Design overview and doc index](design.md)
- [Concurrency and async](async.md)
- [Native modules](native-modules.md)
- [Build and drivers](build.md)
- [Official libraries](official-libraries.md)
- [Security notes](security.md)
- [WASM HTTP roadmap](wasm-http-roadmap.md)
