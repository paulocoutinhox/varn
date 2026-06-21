-- streams a file through an open handle writing in chunks then reading them back without loading it all at once
local async = require("async")
local fs = require("fs")

async.run(function()
    local base = fs.mkdtemp("build/_fs_handle_"):await()
    local path = base .. "/stream.bin"

    -- write the file in two chunks through a single handle.
    local writer = fs.open(path, "w"):await()
    writer:write("chunk-one "):await()
    writer:write("chunk-two"):await()
    writer:close():await()

    -- read it back in small reads until end of file yields an empty string.
    local reader = fs.open(path, "r"):await()
    local parts = {}
    while true do
        local chunk = reader:read(4):await()
        if chunk == "" then
            break
        end
        parts[#parts + 1] = chunk
    end
    reader:close():await()
    print("streamed content:", table.concat(parts))

    fs.removeRecursive(base):await()
    print("fs open handle ok")
end)
