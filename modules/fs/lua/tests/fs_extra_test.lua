-- fs extra: exercises stat, readdir, append, copy, rename, and mkdtemp against a scratch directory.
local async = require("async")
local fs = require("fs")

local dir = assert(os.getenv("VARN_TEST_DIR"), "VARN_TEST_DIR is not set; run tests with: python3 varn.py test")

async.run(function()
    local path = dir .. "/fs_extra.txt"
    local payload = "varn-fs-extra\n"

    -- write a file then stat it for size, kind, and a plausible mtime.
    fs.writeFile(path, payload):await()
    local info, serr = fs.stat(path):await()
    assert(not serr, serr)
    assert(info.size == #payload, "stat size mismatch")
    assert(info.isFile, "stat should report a file")
    assert(not info.isDir, "stat should not report a directory")
    assert(info.mtime > 0, "stat mtime should be positive")

    -- a missing path rejects rather than returning a table.
    local _, merr = fs.stat(dir .. "/fs_extra_missing.txt"):await()
    assert(merr, "stat on a missing path should reject")

    -- mkdir then readdir lists the entries by name.
    local sub = dir .. "/fs_extra_dir"
    fs.mkdir(sub):await()
    fs.writeFile(sub .. "/one.txt", "1"):await()
    fs.writeFile(sub .. "/two.txt", "2"):await()
    local names, derr = fs.readdir(sub):await()
    assert(not derr, derr)
    local seen = {}
    for _, name in ipairs(names) do
        seen[name] = true
    end
    assert(seen["one.txt"], "readdir should list one.txt")
    assert(seen["two.txt"], "readdir should list two.txt")

    -- append extends the file and the read-back includes both parts.
    fs.append(path, "more"):await()
    local appended, aerr = fs.readFile(path):await()
    assert(not aerr, aerr)
    assert(appended == payload .. "more", "append round-trip mismatch")

    -- copy duplicates the bytes and the two files compare equal.
    local copyPath = dir .. "/fs_extra_copy.txt"
    fs.copy(path, copyPath):await()
    local copied = fs.readFile(copyPath):await()
    assert(copied == appended, "copy content mismatch")

    -- rename moves the file so the old name is gone and the new one exists.
    local renamed = dir .. "/fs_extra_renamed.txt"
    fs.rename(copyPath, renamed):await()
    assert(not fs.exists(copyPath), "rename should remove the source")
    assert(fs.exists(renamed), "rename should create the destination")

    -- mkdtemp creates a fresh unique directory under the given prefix.
    local tempDir, terr = fs.mkdtemp(dir .. "/fs_extra_tmp_"):await()
    assert(not terr, terr)
    assert(fs.exists(tempDir), "mkdtemp directory should exist")
    local tstat = fs.stat(tempDir):await()
    assert(tstat.isDir, "mkdtemp should create a directory")

    os.remove(path)
    os.remove(renamed)
    print("fs extra ok")
end)
