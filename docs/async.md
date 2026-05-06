# Concurrency and async

## Threading model

| Thread | Role |
|--------|------|
| **Main / event-loop thread** | Owns `lua_State*`. Runs the user script startup, `EventLoop` jobs, and Lua coroutine resumes after `Promise` settlement. |
| **HTTP transport threads** | Accept and parse requests for the **`http`** module; each request becomes a `HttpRequest` and is **posted** to the main loop. |
| **Task pool workers** | Run blocking jobs (for example `fs.readFile`, `http.client.request`, `socket.tcp` I/O). They must **not** touch Lua. |

## Promise and `:await()`

- Native async functions (for example `fs.readFile`) return a **userdata** with metatable `varn.Promise`.
- `:await()` may only be called from a **Lua coroutine** (not the main thread state used as an ordinary call stack for the entry script, unless that script itself is written as a coroutine). In practice, **`http`** handlers run as coroutines resumed by the runtime, so `fs.readFile(...):await()` is valid inside `http.createServer` callbacks when the HTTP stack is fully enabled.
- After checking for an early settlement the prepare await path registers the current coroutine under the promise lock so a racing resolve cannot leave the coroutine suspended forever.

## Settlement rules

- Each promise settles **at most once** to either resolved or rejected. A second `resolve` or `reject` is ignored (no-op) after settlement.
- `resolve` / `reject` may be called from any thread; they schedule resumption on the **main** `EventLoop` via `mainLoop().post` (same entry point on all supported hosts).

## Implementing async in C++

1. Allocate `std::make_shared<Promise>(runtime)` (or capture an existing promise).
2. Post work to `runtime.taskPool()` (or another non-Lua thread).
3. From the worker, call `promise->resolve(...)`, `promise->resolveCustom(...)` (to push non-string results such as userdata, on the main loop), or `promise->reject(...)` on failure.
4. Push the promise to Lua with `Promise::push(L, promise)` and return `1`.

Never call the lua stack api from task pool workers.

## `async` module

- **`async.sleep(ms)`** — returns a `Promise` that resolves after `ms` milliseconds. On **native** hosts this uses the **task pool** (not a dedicated timer queue). On **Emscripten**, see [Emscripten (`varn_wasm`)](#emscripten-varn_wasm) below.
- **`async.spawn(fn)`** — starts a coroutine from function `fn`. If the coroutine yields, ownership of its registry reference is tied to `Promise` machinery used by `:await()` inside that coroutine.

## Emscripten (`varn_wasm`)

Blocking helpers enqueue onto the shared task pool with the same depth accounting as on desktop builds. No background threads dequeue that work while lua from the current chunk is still running. When the wasm chunk entry returns the host drains the task queue and the main loop queue in repeated passes until a full pass finds no new work then it yields to the browser until any in flight fetches complete. A sleep promise therefore stays pending until its timer job runs which keeps quick `isDone` checks aligned with desktop expectations while still treating `isDone` as a hint only.

Resolvers and rejecters still push through the main loop continuation path used on desktop. The explicit drain runs those callbacks because the long running `mainLoop.run` loop is not present in the worker.

Sleep calls the platform delay helper from inside the queued job. Spawn clears the line hook on the child coroutine so nested resume stays compatible with asyncify unwinds from the shell.

## Further reading

- [Architecture overview](architecture.md)
- [Native modules](native-modules.md)
- [Build and drivers](build.md)
