-- fs features: exercises read/write round-trips, existence transitions, overwrite, binary safety, and a missing-file error.
local async = require("async")
local fs = require("fs")

local dir = assert(os.getenv("VARN_TEST_DIR"), "VARN_TEST_DIR is not set; run tests with: python3 varn.py test")

async.run(function()
    local path = dir .. "/fs_features.txt"
    local payload = "varn-fs-features\n"

    -- write then read back must round-trip exactly.
    fs.writeFile(path, payload):await()
    assert(fs.exists(path), "file should exist after write")
    local content, err = fs.readFile(path):await()
    assert(not err, err)
    assert(content == payload, "round-trip content mismatch")

    -- overwrite replaces the previous content rather than appending.
    local replacement = "second-write"
    fs.writeFile(path, replacement):await()
    local after, oerr = fs.readFile(path):await()
    assert(not oerr, oerr)
    assert(after == replacement, "overwrite should replace content")

    -- a few KB of content round-trips under the 256 MB read cap.
    local big = string.rep("varn-block-", 1000)
    fs.writeFile(path, big):await()
    local bigBack, berr = fs.readFile(path):await()
    assert(not berr, berr)
    assert(bigBack == big, "large content round-trip mismatch")
    assert(#bigBack == #big, "large content length mismatch")

    -- a normal file reads fine, confirming the cap does not block ordinary reads.
    local normal, nerr = fs.readFile(path):await()
    assert(not nerr, nerr)
    assert(#normal > 0, "normal read should return content")

    -- binary content with embedded NUL bytes survives the round-trip intact.
    local binary = "a\0b\0c"
    fs.writeFile(path, binary):await()
    local binBack = fs.readFile(path):await()
    assert(binBack == binary, "binary round-trip mismatch")
    assert(#binBack == 5, "binary length should be preserved")

    -- reading a missing path returns a clean error rather than crashing.
    os.remove(path)
    assert(not fs.exists(path), "file should be gone after remove")
    local missing, merr = fs.readFile(path):await()
    assert(missing == nil, "missing read should yield nil content")
    assert(merr, "missing read should return an error")

    -- exists reports false for an absent path and true only after a write.
    assert(not fs.exists(path), "exists should be false before write")
    fs.writeFile(path, "x"):await()
    assert(fs.exists(path), "exists should be true after write")
    os.remove(path)

    print("fs features ok")
    os.exit(0)
end)
