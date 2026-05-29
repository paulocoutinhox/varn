-- creates a small archive and unpacks it, verifying the entry round-trips.
local async = require("async")
local zip = require("zip")

async.run(function()
    local src = "build/_fixture_hello.txt"
    local archive = "build/_test_roundtrip.zip"
    local outDir = "build/_extract_out"

    local f = assert(io.open(src, "w"), "cannot write fixture")
    f:write("zip-roundtrip\n")
    f:close()

    os.execute("rm -rf '" .. outDir .. "' && mkdir -p '" .. outDir .. "'")

    local _, createErr = zip.create(archive, { { file = src, entry = "inside/hello.txt" } }):await()
    assert(not createErr, createErr)

    local _, extractErr = zip.extract(archive, outDir):await()
    assert(not extractErr, extractErr)

    local rf = assert(io.open(outDir .. "/inside/hello.txt", "r"), "extracted file missing")
    local body = rf:read("*a")
    rf:close()
    assert(body == "zip-roundtrip\n", "content mismatch")

    os.execute("rm -rf '" .. outDir .. "'")
    os.remove(archive)
    os.remove(src)

    print("zip create/extract roundtrip ok")
end)
