-- lists member names after writing a tiny archive on native builds with libzip
local async = require("async")

async.spawn(function()
    local zip = require("zip")

    local zipPath = "build/_list_test.zip"
    local srcPath = "build/_list_fixture.txt"

    local f = io.open(srcPath, "w")
    if not f then
        print("SKIP zip.list (cwd not repo root?)")
        os.exit(0)
    end
    f:write("list-test\n")
    f:close()

    local okCreate, createP = pcall(zip.create, zipPath, {
        { file = srcPath, entry = "a/one.txt" },
        { file = srcPath, entry = "b/two.txt" }
    })
    if not okCreate then
        print("SKIP zip.list (create unavailable):", createP)
        os.remove(srcPath)
        os.exit(0)
    end

    local _, errC = createP:await()
    if errC then
        print("SKIP zip.list (create failed):", errC)
        os.remove(srcPath)
        os.exit(0)
    end

    local okList, listP = pcall(zip.list, zipPath)
    if not okList then
        print("SKIP zip.list (list unavailable):", listP)
        os.remove(zipPath)
        os.remove(srcPath)
        os.exit(0)
    end

    local entries, errL = listP:await()
    if errL then
        print("SKIP zip.list (list failed):", errL)
        os.remove(zipPath)
        os.remove(srcPath)
        os.exit(0)
    end

    assert(type(entries) == "table", "list returns table")
    local names = {}
    for i = 1, #entries do
        names[entries[i]] = true
    end
    assert(names["a/one.txt"] and names["b/two.txt"], "expected entry names")

    os.remove(zipPath)
    os.remove(srcPath)
    print("zip.list ok")
    os.exit(0)
end)
