-- a command given a deadline is killed when it passes it, so an endless child cannot hold an io pool thread for the life of the process
local async = require("async")
local platform = require("platform")
local process = require("process")

if not process.available then
    print("process not available on this build, skipping")
    return
end

-- a command that sleeps well past its deadline without ever writing, which is the case a drain alone would wait on forever
local function sleeper(seconds)
    if platform.os() == "windows" then
        return string.format("ping -n %d 127.0.0.1 > nul", seconds + 1)
    end

    return string.format("sleep %d", seconds)
end

async.run(function()
    -- a command that finishes inside its deadline is unaffected
    local quick = process.exec("echo inside", { timeoutMs = 30000 }):await()
    assert(quick.code == 0, "a command within its deadline should succeed, got " .. quick.code)
    assert(quick.stdout:find("inside", 1, true), "a command within its deadline should return its output")

    -- a command that overruns is killed and the promise rejects rather than resolving with a partial result
    local started = os.time()
    local result, err = process.exec(sleeper(30), { timeoutMs = 300 }):await()
    local elapsed = os.time() - started

    assert(result == nil, "an overrunning command must not resolve")
    assert(err ~= nil, "an overrunning command must report an error")
    assert(err:find("timeout", 1, true), "the error should name the timeout, got " .. tostring(err))
    assert(elapsed < 20, "the deadline should end the wait promptly, took " .. elapsed .. "s")

    -- with no timeout the call still behaves exactly as before
    local unbounded = process.exec("echo unbounded"):await()
    assert(unbounded.code == 0, "an untimed command should still run")
    assert(unbounded.stdout:find("unbounded", 1, true), "an untimed command should still return its output")

    -- a negative deadline is refused at the call rather than being treated as unbounded
    local ok = pcall(function()
        return process.exec("echo x", { timeoutMs = -1 })
    end)
    assert(not ok, "a negative timeout should be rejected")

    print("process timeout ok")
end)
