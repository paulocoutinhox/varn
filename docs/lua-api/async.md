# ⏳ async

Coroutine-based concurrency over the event loop. Async functions return promises.

- `async.sleep(ms)` → a promise that resolves after `ms` milliseconds.
- `async.spawn(fn)` → run `fn` as a background coroutine that can `:await()` promises.
- `async.run(fn)` → run `fn` as the program's async entry point. The process exits when it returns, or with a non-zero status if an uncaught error escapes.

## Promises

- `promise:await()` — pauses the current coroutine until the promise settles, then returns the value (or `nil, err` on failure).
- `promise:isDone()` → boolean. Treat it as a hint, not as synchronization.

See also the design notes in [../async.md](../async.md).

## Examples

### `promise_isdone.lua`

```lua
local async = require("async")

async.spawn(function()
    local p = async.sleep(5)
    assert(p:isDone() == false, "sleep promise should start pending")
    p:await()
    assert(p:isDone() == true, "sleep promise should be done after await")
    print("promise:isDone ok")
end)
```

### `sleep.lua`

```lua
local async = require("async")

async.spawn(function()
    local t0 = os.clock()
    async.sleep(50):await()
    local dt = (os.clock() - t0) * 1000
    print("async.sleep ok (requested 50ms, os.clock delta " .. string.format("%.1f", dt) .. " ms)")
end)
```

### `spawn.lua`

```lua
local async = require("async")

local done = false

async.spawn(function()
    assert(coroutine.isyieldable(), "spawned fn should run as coroutine")
    done = true
end)

async.spawn(function()
    async.sleep(1):await()
    assert(done, "inner spawn should have run")
    print("async.spawn ok")
end)
```
