-- fs security: confirms the non-sandboxed fs API handles hostile paths and content consistently and never crashes.
-- covered classes: path traversal (CWE-22), NUL/control bytes in path (CWE-626, CWE-74), empty path (CWE-20),
-- covered classes: missing-file errors (CWE-20), large-content round-trip near the read cap (CWE-400), binary content (CWE-626).
local async = require("async")
local fs = require("fs")

local dir = assert(os.getenv("VARN_TEST_DIR"), "VARN_TEST_DIR is not set; run tests with: python3 varn.py test")
local leaf = dir:gsub("[/\\]+$", ""):match("[^/\\]+$")

async.run(function()
    -- FS-001 path traversal: a `../` path that resolves back inside the scratch dir is read consistently.
    local base = dir .. "/fs_sec_base.txt"
    local payload = "fs-sec-traversal"
    fs.writeFile(base, payload):await()
    local viaTraversal, terr = fs.readFile(dir .. "/../" .. leaf .. "/fs_sec_base.txt"):await()
    assert(not terr, terr)
    assert(viaTraversal == payload, "traversal-resolved read should match the original")
    os.remove(base)

    -- FS-007 nested traversal: an escaping `../` path that points nowhere errors cleanly instead of crashing.
    local escaped, eerr = fs.readFile(dir .. "/../../../fs_sec_nope_zzz.txt"):await()
    assert(escaped == nil, "escaping traversal read should yield nil content")
    assert(eerr, "escaping traversal read should return an error")

    -- FS-057 missing file: reading an absent path rejects with a clear error.
    local missing, merr = fs.readFile(dir .. "/fs_sec_missing_zzz.txt"):await()
    assert(missing == nil, "missing read should yield nil content")
    assert(merr, "missing read should return an error")

    -- FS-008, FS-161 NUL in path: an embedded NUL is handled as an unreadable path, not a truncation crash.
    local nulRead, nerr = fs.readFile(dir .. "/fs_sec\0.txt"):await()
    assert(nulRead == nil, "NUL path read should yield nil content")
    assert(nerr, "NUL path read should return an error")

    -- FS-014 control bytes in path: a newline in the path is handled without crashing.
    local ctrlRead, cerr = fs.readFile(dir .. "/fs_sec\n_ctrl.txt"):await()
    assert(ctrlRead == nil, "control-byte path read should yield nil content")
    assert(cerr, "control-byte path read should return an error")

    -- FS-015 empty path: an empty string is rejected cleanly.
    local emptyRead, perr = fs.readFile(""):await()
    assert(emptyRead == nil, "empty path read should yield nil content")
    assert(perr, "empty path read should return an error")

    -- FS-038 read cap reachable behavior: a sub-cap file reads in full, confirming ordinary reads stay under the limit.
    local capPath = dir .. "/fs_sec_cap.txt"
    local block = string.rep("cap-", 4096)
    fs.writeFile(capPath, block):await()
    local capBack, kerr = fs.readFile(capPath):await()
    assert(not kerr, kerr)
    assert(#capBack == #block, "sub-cap read should return the full content")
    os.remove(capPath)

    -- FS-050, FS-162 NUL in content: binary content with NUL bytes is written and read back without truncation.
    local binPath = dir .. "/fs_sec_bin.txt"
    local binary = "x\0y\0z\0"
    fs.writeFile(binPath, binary):await()
    local binBack = fs.readFile(binPath):await()
    assert(binBack == binary, "binary content should round-trip without truncation")
    assert(#binBack == 6, "binary content length should be preserved")
    os.remove(binPath)

    print("fs security ok")
    os.exit(0)
end)
