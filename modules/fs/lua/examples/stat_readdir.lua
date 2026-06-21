-- demonstrates stat readdir append copy rename and mkdtemp under a temporary directory
local async = require("async")
local fs = require("fs")

async.run(function()
    -- create a private scratch directory so the example leaves nothing behind elsewhere.
    local base = fs.mkdtemp("build/_fs_extra_"):await()
    print("scratch dir:", base)

    local file = base .. "/note.txt"
    fs.writeFile(file, "hello"):await()

    -- stat reports the size and kind of the file.
    local info = fs.stat(file):await()
    print("size:", info.size, "isFile:", info.isFile, "mtime:", info.mtime)

    -- append extends the file in place.
    fs.append(file, " world"):await()
    print("after append:", fs.readFile(file):await())

    -- copy then rename moves the bytes around within the scratch directory.
    fs.copy(file, base .. "/copy.txt"):await()
    fs.rename(base .. "/copy.txt", base .. "/renamed.txt"):await()

    -- readdir lists the entries by name.
    local names = fs.readdir(base):await()
    table.sort(names)
    print("entries:", table.concat(names, ", "))

    fs.removeRecursive(base):await()
    print("fs extra example ok")
end)
