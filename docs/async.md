# Concurrency and async

This page explains how Varn runs your code, what is a real thread and what is not, and the
rules to follow so you do not block or crash the runtime.

## The one rule: Lua lives on a single thread

Varn keeps one `lua_State` (the Lua interpreter) and only ever touches it from one thread, the
main thread. Your script, your HTTP handlers, timers, and promise results all run there, one
at a time. This is on purpose. Lua is not thread-safe, so a single owner thread removes a whole
class of races and crashes.

The trade-off: if you do something slow on that thread, like a long loop or a blocking read,
nothing else runs until it finishes. The fix is to move slow work off the main thread and get
the result back through a promise.

## Threads vs coroutines

These two are easy to mix up.

- A thread is a real OS thread that runs in parallel. Varn keeps a small pool of them for
  blocking work, and they never touch Lua.
- A coroutine is a Lua feature: a function that can pause and resume on the same thread. It is
  not parallel. HTTP handlers and `async.spawn` functions run as coroutines on the main thread.

So `async.spawn(fn)` does not start a new thread. It starts a coroutine that shares the main
thread and cooperatively pauses at each `:await()`.

## What runs where

| Runs on | What |
|---------|------|
| Main thread (owns Lua) | Your script, HTTP handlers, `async.spawn` coroutines, timers, and the code that resumes a coroutine after a promise settles. |
| HTTP transport threads | Accept and parse incoming requests. Each finished request is handed to the main thread to run your handler. |
| Task pool (real threads) | Blocking work: `fs` reads/writes, `http.client.request`, socket I/O, `zip`. These never call Lua. They produce a result and hand it back. |

## Promises and `:await()`

A function that does slow work returns a promise instead of blocking. Call `:await()` to pause
until it finishes and get the value:

```lua
local fs = require("fs")
local async = require("async")

async.spawn(function()
  local text, err = fs.readFile("notes.txt"):await()
  if err then
    print("could not read:", err)
    return
  end
  print(text)
end)
```

`:await()` pauses the current coroutine and lets the main thread keep working. When the task
pool finishes the read, the result is posted back to the main thread and your coroutine resumes
right after the `:await()`.

`:await()` only works inside a coroutine, which means inside an HTTP handler or an
`async.spawn` function. It does not work at the top level of a plain script, because there is no
coroutine to pause. A promise settles exactly once, either resolved or rejected. Later settles
are ignored.

## The async module

- `async.sleep(ms)` returns a promise that resolves after `ms` milliseconds without blocking
  the thread.
- `async.spawn(fn)` runs `fn` as a coroutine so you can `:await()` outside an HTTP handler.

```lua
local async = require("async")

async.spawn(function()
  print("start")
  async.sleep(500):await() -- the thread stays free during this pause
  print("half a second later")
end)

print("this line runs first, before the sleep finishes")
```

## Do and don't

Do:

- Use `:await()` for I/O (files, network, sockets, zip). It keeps the server responsive.
- Wrap independent work in `async.spawn` so the pieces can interleave.
- Read the second return of `:await()` for errors, as in `local value, err = p:await()`.

Don't:

- Don't run heavy CPU work or a busy loop on the main thread. It blocks every other request
  until it ends. Keep handlers short, or break the work up.
- Don't fake waiting with `os.execute` or a manual busy-loop. Use `async.sleep` or `:await()`.
- Don't call `:await()` at the top level of a script. Put it inside `async.spawn`.

## Writing an async function in C++

1. Create a promise with `std::make_shared<varn::async::Promise>(runtime)`.
2. Post the blocking work to `runtime.taskPool()`.
3. From the worker, call `promise->resolve(...)`, or `promise->resolveCustom(...)` for a
   non-string result such as userdata, or `promise->reject(...)` on failure.
4. Push the promise to Lua with `varn::async::Promise::push(L, promise)` and return 1.

The worker thread must never touch the Lua stack. Only the closure that runs back on the main
loop (through `resolveCustom`) may push Lua values.

## WebAssembly is different (varn_wasm)

The browser build has no real threads, so the model shifts in ways worth knowing:

- The task pool is not parallel. Blocking helpers queue their work instead of running it, and
  nothing runs while your current Lua chunk is still executing.
- When your chunk returns, the host drains the task queue and the main-loop queue in repeated
  passes until nothing new appears, then waits for any in-flight `fetch` to finish. Promises
  still settle through the same main-loop path as on desktop.
- `async.sleep` uses the browser timer. A sleep promise stays pending until it fires, so
  `:isDone()` behaves as on desktop. Treat `:isDone()` as a hint, never as a lock.
- There is no HTTP server and no raw TCP socket in the browser. `http.client.request` uses the
  browser `fetch()` API. `crypto` and `ffi` are stubs that error when called, and TLS is off.

In short, the same Lua code runs, but anything that needs an OS thread, a listening socket, or
native crypto is queued, routed through the browser, or unavailable.

## Further reading

- [How it works](architecture.md)
- [Adding a C++ module](native-modules.md)
- [Building](build.md)
