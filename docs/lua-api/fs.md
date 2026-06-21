# 📁 fs

Filesystem access. Reads and writes run off the main loop and return promises.

- `fs.readFile(path)` → promise resolving to the file contents (binary-safe). Reads the whole file with no artificial size cap, like Node.
- `fs.writeFile(path, content)` → promise resolving to `"ok"`.
- `fs.exists(path)` → boolean, checked synchronously.
- `fs.open(path, mode)` → promise resolving to a streaming file handle. `mode` is one of `r`, `w`, `a`, `r+`, `w+`, `a+`. Use this instead of `readFile` for large files so the whole content never sits in memory at once:
  - `handle:read(maxBytes)` → promise resolving to up to `maxBytes` bytes (binary-safe), or an empty string at end of file.
  - `handle:write(data)` → promise resolving to `"ok"`.
  - `handle:close()` → promise resolving to `"ok"`.
  Await each handle call before issuing the next, the same way you would with a Node file handle.

Paths are not sandboxed; confine untrusted input before passing it here. A path containing a null byte is rejected rather than silently truncated.

## Examples

### `read_write_exists.lua`

```lua
-- reads writes and probes existence on a path under the repository tree
local async = require("async")
local fs = require("fs")

local marker = "modules/fs/lua/examples/read_write_exists.lua"

async.spawn(function()
    assert(fs.exists(marker), "run from repo root (missing " .. marker .. ")")

    local tmp = "build/_roundtrip.txt"
    fs.writeFile(tmp, "hello-fs\n"):await()
    assert(fs.exists(tmp), "tmp should exist")

    local content, err = fs.readFile(tmp):await()
    assert(not err, err)
    assert(content == "hello-fs\n", "content mismatch")

    os.remove(tmp)
    assert(not fs.exists(tmp), "tmp should be removed")

    print("fs read/write/exists ok")
end)
```

## Under the hood

Built on the C++ standard library filesystem, with reads and writes running on a background I/O thread pool so they never block the event loop.
