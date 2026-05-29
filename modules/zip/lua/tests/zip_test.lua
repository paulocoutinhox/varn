-- zip: create an archive, list it, extract it, and verify the entry round-trips.
local async = require("async")

async.spawn(function()
    local zip = require("zip")

    local src = "build/_zip_src.txt"
    local archive = "build/_zip_test.zip"
    local outDir = "build/_zip_out"

    local f = assert(io.open(src, "w"), "cannot create fixture")
    f:write("zip-test\n")
    f:close()

    os.execute("rm -rf '" .. outDir .. "' && mkdir -p '" .. outDir .. "'")

    local _, createErr = zip.create(archive, { { file = src, entry = "inside/data.txt" } }):await()
    assert(not createErr, createErr)

    local entries, listErr = zip.list(archive):await()
    assert(not listErr, listErr)
    assert(type(entries) == "table" and #entries == 1 and entries[1] == "inside/data.txt", "list entry")

    local _, extractErr = zip.extract(archive, outDir):await()
    assert(not extractErr, extractErr)

    local rf = assert(io.open(outDir .. "/inside/data.txt", "r"), "extracted file missing")
    local body = rf:read("*a")
    rf:close()
    assert(body == "zip-test\n", "extracted content mismatch")

    os.execute("rm -rf '" .. outDir .. "'")
    os.remove(archive)
    os.remove(src)

    print("zip ok")
    os.exit(0)
end)
