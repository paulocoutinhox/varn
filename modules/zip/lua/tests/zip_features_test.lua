-- zip: exercises create/list/extract round-trips, multiple and nested entries, and reachable caps.
local async = require("async")

local dir = assert(os.getenv("VARN_TEST_DIR"), "VARN_TEST_DIR is not set; run tests with: python3 varn.py test")

async.run(function()
    local zip = require("zip")

    local src = dir .. "/zip_feat_src.txt"
    local empty = dir .. "/zip_feat_empty.txt"
    local archive = dir .. "/zip_feat.zip"
    local outDir = dir .. "/zip_feat_out"

    local f = assert(io.open(src, "w"), "cannot create fixture")
    f:write("zip-features\n")
    f:close()

    -- an empty source file is a valid regular file and must round-trip too.
    local ef = assert(io.open(empty, "w"), "cannot create empty fixture")
    ef:close()

    -- create an archive with multiple entries, including nested paths and an empty member.
    local _, createErr = zip.create(archive, {
        { file = src, entry = "top.txt" },
        { file = src, entry = "nested/deep/inside.txt" },
        { file = empty, entry = "empty.txt" },
    }):await()
    assert(not createErr, createErr)

    -- list returns every entry name back unsanitized for the caller.
    local entries, listErr = zip.list(archive):await()
    assert(not listErr, listErr)
    assert(type(entries) == "table" and #entries == 3, "expected three entries")

    local names = {}
    for i = 1, #entries do
        names[entries[i]] = true
    end
    assert(names["top.txt"], "top entry listed")
    assert(names["nested/deep/inside.txt"], "nested entry listed")
    assert(names["empty.txt"], "empty entry listed")

    -- extract reproduces the tree and the original contents byte for byte.
    local _, extractErr = zip.extract(archive, outDir):await()
    assert(not extractErr, extractErr)

    local rf = assert(io.open(outDir .. "/nested/deep/inside.txt", "r"), "nested file missing")
    local body = rf:read("*a")
    rf:close()
    assert(body == "zip-features\n", "nested content mismatch")

    local eo = assert(io.open(outDir .. "/empty.txt", "r"), "empty file missing")
    assert(eo:read("*a") == "", "empty member should be empty")
    eo:close()

    -- an empty entries list is rejected synchronously, before any promise is created.
    assert(not pcall(zip.create, archive, {}), "empty entries list rejected")

    -- an unsafe entry name is rejected at create time, never written to the archive.
    local _, unsafeErr = zip.create(archive, { { file = src, entry = "../escape.txt" } }):await()
    assert(unsafeErr, "unsafe create entry rejected")

    -- listing a missing archive returns an error, not a crash.
    local _, missingErr = zip.list(dir .. "/zip_feat_missing.zip"):await()
    assert(missingErr, "missing archive list errors")

    print("zip features ok")
end)
