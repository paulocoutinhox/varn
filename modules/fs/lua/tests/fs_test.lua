-- fs: write then read back a temp file and confirm the existence transitions.
local async = require("async")
local fs = require("fs")

async.spawn(function()
    local path = "build/_fs_test.txt"
    local payload = "varn-fs-test\n"

    fs.writeFile(path, payload):await()
    assert(fs.exists(path), "file should exist after write")

    local content, err = fs.readFile(path):await()
    assert(not err, err)
    assert(content == payload, "round-trip content mismatch")

    os.remove(path)
    assert(not fs.exists(path), "file should be gone after remove")

    print("fs ok")
    os.exit(0)
end)
