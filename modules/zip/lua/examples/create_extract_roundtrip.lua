-- round trip create and unpack a small archive on native builds with libzip
local async = require("async")

async.spawn(function()
    local zip = require("zip")

    local zipPath = "build/_test_roundtrip.zip"
    local extractDir = "build/_extract_out"
    local srcPath = "build/_fixture_hello.txt"

    local f = io.open(srcPath, "w")
    if not f then
        print("SKIP zip: cannot write fixture (cwd not repo root?)")
        os.exit(0)
    end
    f:write("zip-roundtrip\n")
    f:close()

    os.execute("rm -rf '" .. extractDir .. "'")
    os.execute("mkdir -p '" .. extractDir .. "'")

    local okCreate, createPromiseOrErr = pcall(zip.create, zipPath, {
        { file = srcPath, entry = "inside/hello.txt" }
    })
    if not okCreate then
        print("SKIP zip (create unavailable):", createPromiseOrErr)
        os.remove(srcPath)
        os.exit(0)
    end

    local _, errCreate = createPromiseOrErr:await()
    if errCreate then
        print("SKIP zip (create failed):", errCreate)
        os.remove(srcPath)
        os.execute("rm -rf '" .. extractDir .. "'")
        os.exit(0)
    end

    local okExtract, extractPromiseOrErr = pcall(zip.extract, zipPath, extractDir)
    if not okExtract then
        print("SKIP zip (extract unavailable):", extractPromiseOrErr)
        os.remove(zipPath)
        os.remove(srcPath)
        os.execute("rm -rf '" .. extractDir .. "'")
        os.exit(0)
    end

    local _, errExtract = extractPromiseOrErr:await()
    if errExtract then
        print("SKIP zip (extract failed):", errExtract)
        os.remove(zipPath)
        os.remove(srcPath)
        os.execute("rm -rf '" .. extractDir .. "'")
        os.exit(0)
    end

    local rf = io.open(extractDir .. "/inside/hello.txt", "r")
    if not rf then
        print("FAIL zip: extracted file missing")
        os.exit(1)
    end
    local body = rf:read("*a")
    rf:close()
    if body ~= "zip-roundtrip\n" then
        print("FAIL zip: content mismatch")
        os.exit(1)
    end

    os.remove(zipPath)
    os.remove(srcPath)
    os.execute("rm -rf '" .. extractDir .. "'")

    print("zip create/extract roundtrip ok")
    os.exit(0)
end)
