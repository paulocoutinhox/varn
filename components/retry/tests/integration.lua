-- assert-based test for retry: backoff retry succeeding after failures, exhausting attempts, succeeding on the first try, awaiting a returned promise, a one-shot timer firing and being cancellable, and a repeating interval that can be stopped. runs inside async.run since every timer yields on the event loop.
local dir = arg[0]:match("^(.*)[/\\]") or "."
package.path = ("%s/../../?.lua;%s/../../?/init.lua;"):format(dir, dir) .. package.path

local async = require("async")
local retry = require("retry")

async.run(function()
    -- retries until the function stops failing, returning its value.
    local calls = 0
    local value = retry.run(function()
        calls = calls + 1
        if calls < 3 then
            error("fail " .. calls)
        end
        return "done"
    end, { attempts = 5, backoffMs = 5, factor = 2 })
    assert(value == "done", "retry returns the eventual success value")
    assert(calls == 3, "retry stops as soon as it succeeds")
    print("retry success ok (took " .. calls .. " tries)")

    -- a function that always fails exhausts its attempts and raises.
    local attempts = 0
    local ok, err = pcall(retry.run, function()
        attempts = attempts + 1
        error("permanent")
    end, { attempts = 3, backoffMs = 5 })
    assert(not ok, "exhausted retry raises")
    assert(attempts == 3, "all attempts are used")
    assert(tostring(err):find("permanent"), "final error is surfaced")
    print("retry exhaustion ok")

    -- succeeding on the first try makes exactly one call.
    local once = 0
    local first = retry.run(function()
        once = once + 1
        return 42
    end)
    assert(first == 42 and once == 1, "first-try success calls once")

    -- a returned promise is awaited; a value resolves, an error rejects and retries.
    local promiseCalls = 0
    local resolved = retry.run(function()
        promiseCalls = promiseCalls + 1
        -- async.sleep returns a promise; the run awaits it and treats a clean resolution as success, forwarding its value.
        return async.sleep(1)
    end)
    assert(resolved ~= nil and promiseCalls == 1, "a resolving promise counts as success")
    print("retry promise ok")

    -- after() fires once.
    local fired = false
    retry.after(20, function()
        fired = true
    end)
    async.sleep(60):await()
    assert(fired, "after fires once")

    -- a cancelled after() never fires.
    local cancelledFired = false
    local h = retry.after(40, function()
        cancelledFired = true
    end)
    h:cancel()
    async.sleep(80):await()
    assert(not cancelledFired, "cancelled after does not fire")
    assert(h:isCancelled(), "handle reports cancelled")
    print("after ok")

    -- every() ticks repeatedly and stops on cancel.
    local ticks = 0
    local handle = retry.every(15, function()
        ticks = ticks + 1
    end)
    async.sleep(120):await()
    handle:cancel()
    local atCancel = ticks
    assert(atCancel >= 3, "interval ticked several times, got " .. atCancel)
    async.sleep(60):await()
    assert(ticks == atCancel, "no ticks after cancel")
    print("every ok (" .. atCancel .. " ticks before cancel)")

    print("retry integration ok")
end)
