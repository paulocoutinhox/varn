-- retry: retry-with-backoff plus a tiny scheduler, built on async. retry.run re-invokes a function until it succeeds or runs out of attempts, sleeping between tries with exponential backoff; retry.after and retry.every schedule one-shot and repeating work on background coroutines, each returning a handle that can be cancelled. everything yields on the event loop, so it must run inside an async coroutine (async.spawn / async.run).
local async = require("async")

local retry = {}

-- calls fn and normalizes the outcome: a function that raises is a failure, and a function that returns a promise is awaited so a rejected promise is also a failure. returns ok, result-or-error.
local function callOnce(fn)
    local ok, value = pcall(fn)
    if not ok then
        return false, value
    end

    -- a promise (anything with :await) is awaited so async failures count as retryable too.
    if type(value) == "table" and type(value.await) == "function" then
        local awaited, err = value:await()
        if err ~= nil then
            return false, err
        end
        return true, awaited
    end

    return true, value
end

-- run(fn, opts) calls fn, retrying on failure up to opts.attempts times (default 3) with an exponential backoff starting at opts.backoffMs (default 100) and multiplied by opts.factor (default 2) each retry. fn may return a promise. on success returns its value; after the last failed attempt it re-raises the final error.
function retry.run(fn, opts)
    opts = opts or {}
    local attempts = opts.attempts or 3
    local backoff = opts.backoffMs or 100
    local factor = opts.factor or 2

    local lastError
    for attempt = 1, attempts do
        local ok, value = callOnce(fn)
        if ok then
            return value
        end

        lastError = value
        -- sleep before every retry but not after the final failure.
        if attempt < attempts then
            async.sleep(backoff):await()
            backoff = backoff * factor
        end
    end

    error("[Retry] All " .. attempts .. " attempt(s) failed: " .. tostring(lastError), 0)
end

-- a handle for scheduled work: :cancel() stops a pending or repeating task, and :isCancelled() reports its state.
local Handle = {}
Handle.__index = Handle

function Handle:cancel()
    self.cancelled = true
end

function Handle:isCancelled()
    return self.cancelled
end

local function newHandle()
    return setmetatable({ cancelled = false }, Handle)
end

-- after(ms, fn) runs fn once after ms milliseconds on a background coroutine. returns a handle whose :cancel() prevents fn from firing if called before the delay elapses.
function retry.after(ms, fn)
    local handle = newHandle()
    async.spawn(function()
        async.sleep(ms):await()
        if not handle.cancelled then
            fn()
        end
    end)
    return handle
end

-- every(ms, fn) runs fn repeatedly, waiting ms milliseconds before each run, on a background coroutine. returns a handle whose :cancel() stops the loop; the cancel is observed before fn is called and again before the next sleep, so an in-flight run finishes but no further run starts.
function retry.every(ms, fn)
    local handle = newHandle()
    async.spawn(function()
        while not handle.cancelled do
            async.sleep(ms):await()
            if handle.cancelled then
                break
            end
            fn()
        end
    end)
    return handle
end

return retry
